// Copyright DreamMoon. All Rights Reserved.

#include "DreamNarrativeStory.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "DreamNarrativeRecorder.h"
#include "DreamNarrativeSettings.h"
#include "DreamNarrativeStoryModule.h"
#include "DreamNarrativeTrace.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Subsystems/SubsystemCollection.h"

void UDreamNarrativeStory::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	EnsureMainChannel();
	Ledger = Collection.InitializeDependency<UDreamNarrativeLedger>();
	if (Ledger)
	{
		Ledger->SetQuestStateProvider(this);
		WatchLedger();
	}
}

void UDreamNarrativeStory::InitializeStandalone(UDreamNarrativeLedger* InLedger)
{
	EnsureMainChannel();
	Ledger = InLedger;
	if (Ledger)
	{
		Ledger->SetQuestStateProvider(this);
		WatchLedger();
	}
}

void UDreamNarrativeStory::Deinitialize()
{
	StopRecording();
	StopAllDialogue();
	if (Ledger)
	{
		UnwatchLedger();
		Ledger->SetQuestStateProvider(nullptr);
	}
	Super::Deinitialize();
}

void UDreamNarrativeStory::RegisterScript(UDreamNarrativeScript* Script)
{
	if (!Script || !Ledger)
	{
		return;
	}
	if (!bRegisteringScripts) { Ledger->RegisterScript(Script); }
	Scripts.AddUnique(Script);

	if (const UDnaQuestSection* Quests = Script->FindSection<UDnaQuestSection>())
	{
		for (int32 Index = 0; Index < Quests->Quests.Num(); ++Index)
		{
			const FName Name = Quests->Quests[Index].Name;
			FQuestRef& Ref = QuestIndex.FindOrAdd(Name);
			Ref.Section = Quests;
			Ref.QuestIndex = Index;
			QuestOrder.AddUnique(Name);
		}
	}

	if (const UDnaArchiveSection* Archives = Script->FindSection<UDnaArchiveSection>())
	{
		for (int32 Index = 0; Index < Archives->Archives.Num(); ++Index)
		{
			const FName Name = Archives->Archives[Index].Name;
			FArchiveRef& Ref = ArchiveIndex.FindOrAdd(Name);
			Ref.Section = Archives;
			Ref.ArchiveIndex = Index;
			ArchiveOrder.AddUnique(Name);
		}
	}

	if (const UDnaDialogueSection* Dialogue = Script->FindSection<UDnaDialogueSection>())
	{
		for (const FDnaChannelData& Channel : Dialogue->Channels)
		{
			if (Channel.Name == ::DreamNarrative::MainChannelName())
			{
				// main is built in: a script may only say what it pauses and stops.
				FDnaChannelData& Main = ChannelDecls.FindChecked(Channel.Name);
				Main.Pauses = Channel.Pauses;
				Main.Stops = Channel.Stops;
				continue;
			}
			ChannelDecls.Add(Channel.Name, Channel);
			ChannelOrder.AddUnique(Channel.Name);
			Channels.FindOrAdd(Channel.Name);
		}
		for (int32 Index = 0; Index < Dialogue->Nodes.Num(); ++Index)
		{
			const FDnaDialogueNodeData& Node = Dialogue->Nodes[Index];
			Ledger->RegisterRedirect(Node.WasName, Node.Name);
			if (!Node.WasName.IsNone())
			{
				const FString Prefix = Node.Name.ToString() + TEXT(".");
				for (const FDnaChoiceGroupData& Group : Node.ChoiceGroups)
				{
					for (const FDnaChoiceData& Choice : Group.Options)
					{
						const FString Id = Choice.Id.ToString();
						if (!Choice.bExplicitId && Id.StartsWith(Prefix))
						{
							Ledger->RegisterRedirect(FName(*(Node.WasName.ToString() + TEXT(".") + Id.Mid(Prefix.Len()))), Choice.Id);
						}
					}
				}
			}
			FNodeRef& Ref = NodeIndex.FindOrAdd(Node.Name);
			Ref.Section = Dialogue;
			Ref.NodeIndex = Index;
			if (!Node.ForCharacter.IsNone())
			{
				EntriesByCharacter.FindOrAdd(Node.ForCharacter).AddUnique(Node.Name);
			}
			if (!Node.OnEvent.IsNone())
			{
				EntriesByEvent.FindOrAdd(Node.OnEvent).AddUnique(Node.Name);
			}
		}
	}

	// A hidden quest that can start already does, rather than at the first write -- including one whose
	// condition reads facts only this script declares.
	if (!bRegisteringScripts && !Ledger->IsApplyingSnapshot())
	{
		CheckQuests();
		CheckArchives();
	}
}

void UDreamNarrativeStory::RegisterScripts(const TArray<UDreamNarrativeScript*>& InScripts)
{
	if (!Ledger) { return; }
	{
		TGuardValue<bool> Registering(bRegisteringScripts, true);
		for (UDreamNarrativeScript* Script : InScripts) { Ledger->RegisterScript(Script); }
		for (UDreamNarrativeScript* Script : InScripts) { RegisterScript(Script); }
	}
	if (!bRegisteringScripts && !Ledger->IsApplyingSnapshot())
	{
		CheckQuests();
		CheckArchives();
	}
}

void UDreamNarrativeStory::BindSpeaker(FName Character, AActor* Actor)
{
	if (Actor) { Speakers.Add(Character, Actor); }
	else { Speakers.Remove(Character); }
}

AActor* UDreamNarrativeStory::FindSpeaker(FName Character) const
{
	if (const TWeakObjectPtr<AActor>* Bound = Speakers.Find(Character))
	{
		if (AActor* Actor = Bound->Get()) { return Actor; }
	}
	const UGameInstance* Instance = GetGameInstance();
	UWorld* World = Instance ? Instance->GetWorld() : nullptr;
	if (!World || Character.IsNone())
	{
		return nullptr;
	}
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->ActorHasTag(Character)) { return *It; }
	}
	return nullptr;
}

void UDreamNarrativeStory::HandleVoice(UDreamNarrativeDialogueRunner* Runner, EDnaVoiceEvent Event, const FDnaLineView& Line, AActor* Speaker)
{
	// One mouth: a speaker who begins a line stops their voice on any other channel first.
	if (Event == EDnaVoiceEvent::Requested && !Line.Speaker.IsNone())
	{
		for (const TPair<FName, FDnaChannelState>& Pair : Channels)
		{
			UDreamNarrativeDialogueRunner* Other = Pair.Value.Runner;
			if (Other && Other != Runner && Other->IsVoicePlaying() && Other->GetCurrentLine().Speaker == Line.Speaker)
			{
				Other->StopVoice();
			}
		}
	}
	OnVoice.Broadcast(Runner, Event, Line, Speaker);
}

int32 UDreamNarrativeStory::LoadAllScripts()
{
	const FString& Directory = UDreamNarrativeSettings::Get()->AssetDirectory;
	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	// In the editor and in an uncooked game the registry fills in the background after startup; a
	// query made before it reached this directory would come back short. A cooked game's registry is
	// complete from the start, so this does nothing there.
	if (Registry.IsLoadingAssets())
	{
		Registry.ScanPathsSynchronous({ Directory });
	}
	TArray<FAssetData> Assets;
	FARFilter Filter;
	Filter.ClassPaths.Add(UDreamNarrativeScript::StaticClass()->GetClassPathName());
	Filter.PackagePaths.Add(*Directory);
	Filter.bRecursivePaths = true;
	Registry.GetAssets(Filter, Assets);

	TArray<UDreamNarrativeScript*> Loaded;
	for (const FAssetData& Asset : Assets)
	{
		if (UDreamNarrativeScript* Script = Cast<UDreamNarrativeScript>(Asset.GetAsset()))
		{
			Loaded.Add(Script);
		}
	}
	RegisterScripts(Loaded);
	const int32 Count = Loaded.Num();
	UE_LOG(LogDreamNarrativeStory, Log, TEXT("Registered %d narrative script(s) from %s"), Count, *Directory);
	return Count;
}

const FDnaDialogueNodeData* UDreamNarrativeStory::FindNode(FName Node, const UDnaDialogueSection** OutSection) const
{
	const FNodeRef* Ref = NodeIndex.Find(Node);
	if (!Ref || !Ref->Section.IsValid() || !Ref->Section->Nodes.IsValidIndex(Ref->NodeIndex))
	{
		return nullptr;
	}
	if (OutSection)
	{
		*OutSection = Ref->Section.Get();
	}
	return &Ref->Section->Nodes[Ref->NodeIndex];
}

UDreamNarrativeDialogueRunner* UDreamNarrativeStory::StartDialogue(FName Node)
{
	OnStartRequested.Broadcast(Node);
	return StartOnChannel(Node, nullptr, /*bRequested=*/true);
}

FName UDreamNarrativeStory::SelectEntry(FName Character) const
{
	IDnaTrace* Tracer = Ledger ? Ledger->GetTrace() : nullptr;
	const TArray<FName>* Entries = EntriesByCharacter.Find(Character);
	if (!Entries || !Ledger)
	{
		if (Tracer) { Tracer->OnEvent(TEXT("dialogue.entry.none")); }
		return NAME_None;
	}
	FName Best;
	int32 BestPriority = INT_MIN;
	const FDnaDialogueNodeData* BestNode = nullptr;
	TArray<const FDnaDialogueNodeData*, TInlineAllocator<8>> Applicable;
	for (const FName& Name : *Entries)
	{
		const UDnaDialogueSection* Section = nullptr;
		const FDnaDialogueNodeData* Node = FindNode(Name, &Section);
		if (!Node || !Section || IsEntrySpent(*Node, /*bForEvent=*/false)) { continue; }
		if (Node->Condition != INDEX_NONE)
		{
			const FDnaCompiledExpr* Condition = Section->GetScript() ? Section->GetScript()->GetExpression(Node->Condition) : nullptr;
			if (!Condition || !Ledger->EvaluateCondition(*Condition)) { continue; }
		}
		Applicable.Add(Node);
		if (Node->Priority > BestPriority)
		{
			Best = Name;
			BestPriority = Node->Priority;
			BestNode = Node;
		}
	}
	if (Tracer)
	{
		if (!BestNode)
		{
			Tracer->OnEvent(TEXT("dialogue.entry.none"));
		}
		else
		{
			Tracer->OnEvent(BestNode->Condition != INDEX_NONE ? TEXT("dialogue.entry.conditional") : TEXT("dialogue.entry.fallback"));
			if (BestNode->bExplicitPriority) { Tracer->OnEvent(TEXT("dialogue.entry.explicit-priority")); }
			for (const FDnaDialogueNodeData* Other : Applicable)
			{
				if (Other != BestNode)
				{
					// Equal priority loses to the one declared first; lower priority simply loses.
					Tracer->OnEvent(Other->Priority == BestPriority ? TEXT("dialogue.entry.tie") : TEXT("dialogue.entry.outranked"));
				}
			}
		}
	}
	return Best;
}

UDreamNarrativeDialogueRunner* UDreamNarrativeStory::TalkTo(FName Character)
{
	OnTalkRequested.Broadcast(Character);
	const FName Entry = SelectEntry(Character);
	if (Entry.IsNone())
	{
		UE_LOG(LogDreamNarrativeStory, Log, TEXT("TalkTo: no entry of '%s' applies right now"), *Character.ToString());
		return nullptr;
	}
	return StartOnChannel(Entry, nullptr, /*bRequested=*/true);
}

UDreamNarrativeRecorder* UDreamNarrativeStory::StartRecording(const FString& Name)
{
	StopRecording();
	Recorder = NewObject<UDreamNarrativeRecorder>(this);
	Recorder->Begin(this, Name);
	return Recorder;
}

FString UDreamNarrativeStory::StopRecording()
{
	if (!Recorder)
	{
		return FString();
	}
	UDreamNarrativeRecorder* Stopping = Recorder;
	Recorder = nullptr;
	return Stopping->IsRecording() ? Stopping->End() : FString();
}

