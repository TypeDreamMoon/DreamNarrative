// Copyright DreamMoon. All Rights Reserved.

#include "DreamNarrativeStory.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "DreamNarrativeSettings.h"
#include "DreamNarrativeStoryModule.h"
#include "Engine/GameInstance.h"

void UDreamNarrativeStory::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Ledger = Collection.InitializeDependency<UDreamNarrativeLedger>();
	if (Ledger)
	{
		Ledger->SetQuestStateProvider(this);
	}
}

void UDreamNarrativeStory::InitializeStandalone(UDreamNarrativeLedger* InLedger)
{
	Ledger = InLedger;
	if (Ledger)
	{
		Ledger->SetQuestStateProvider(this);
	}
}

void UDreamNarrativeStory::Deinitialize()
{
	if (Ledger)
	{
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
	Ledger->RegisterScript(Script);
	Scripts.AddUnique(Script);

	const UDnaDialogueSection* Dialogue = Script->FindSection<UDnaDialogueSection>();
	if (!Dialogue)
	{
		return;
	}
	for (int32 Index = 0; Index < Dialogue->Nodes.Num(); ++Index)
	{
		const FDnaDialogueNodeData& Node = Dialogue->Nodes[Index];
		FNodeRef& Ref = NodeIndex.FindOrAdd(Node.Name);
		Ref.Section = Dialogue;
		Ref.NodeIndex = Index;
		if (!Node.ForCharacter.IsNone())
		{
			EntriesByCharacter.FindOrAdd(Node.ForCharacter).AddUnique(Node.Name);
		}
	}
}

int32 UDreamNarrativeStory::LoadAllScripts()
{
	const FAssetRegistryModule& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	TArray<FAssetData> Assets;
	FARFilter Filter;
	Filter.ClassPaths.Add(UDreamNarrativeScript::StaticClass()->GetClassPathName());
	Filter.PackagePaths.Add(*UDreamNarrativeSettings::Get()->AssetDirectory);
	Filter.bRecursivePaths = true;
	Registry.Get().GetAssets(Filter, Assets);

	int32 Count = 0;
	for (const FAssetData& Asset : Assets)
	{
		if (UDreamNarrativeScript* Script = Cast<UDreamNarrativeScript>(Asset.GetAsset()))
		{
			RegisterScript(Script);
			++Count;
		}
	}
	UE_LOG(LogDreamNarrativeStory, Log, TEXT("Registered %d narrative script(s) from %s"), Count, *UDreamNarrativeSettings::Get()->AssetDirectory);
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
	const UDnaDialogueSection* Section = nullptr;
	if (!FindNode(Node, &Section))
	{
		UE_LOG(LogDreamNarrativeStory, Error, TEXT("StartDialogue: no dialogue called '%s'"), *Node.ToString());
		return nullptr;
	}
	if (ActiveRunner && ActiveRunner->GetState() != EDnaRunnerState::Ended)
	{
		ActiveRunner->Abort();
	}
	UDreamNarrativeDialogueRunner* Runner = NewObject<UDreamNarrativeDialogueRunner>(this);
	Runner->OnEnded.AddDynamic(this, &UDreamNarrativeStory::HandleRunnerEnded);
	ActiveRunner = Runner;
	OnDialogueStarted.Broadcast(Runner);
	Runner->Start(this, Ledger, Section, Node);
	return Runner;
}

FName UDreamNarrativeStory::SelectEntry(FName Character) const
{
	const TArray<FName>* Entries = EntriesByCharacter.Find(Character);
	if (!Entries || !Ledger)
	{
		return NAME_None;
	}
	FName Best;
	int32 BestPriority = INT_MIN;
	for (const FName& Name : *Entries)
	{
		const UDnaDialogueSection* Section = nullptr;
		const FDnaDialogueNodeData* Node = FindNode(Name, &Section);
		if (!Node || !Section) { continue; }
		if (Node->Condition != INDEX_NONE)
		{
			const FDnaCompiledExpr* Condition = Section->GetScript() ? Section->GetScript()->GetExpression(Node->Condition) : nullptr;
			if (!Condition || !Ledger->EvaluateCondition(*Condition)) { continue; }
		}
		if (Node->Priority > BestPriority)
		{
			Best = Name;
			BestPriority = Node->Priority;
		}
	}
	return Best;
}

UDreamNarrativeDialogueRunner* UDreamNarrativeStory::TalkTo(FName Character)
{
	const FName Entry = SelectEntry(Character);
	if (Entry.IsNone())
	{
		UE_LOG(LogDreamNarrativeStory, Log, TEXT("TalkTo: no entry of '%s' applies right now"), *Character.ToString());
		return nullptr;
	}
	return StartDialogue(Entry);
}

void UDreamNarrativeStory::HandleRunnerEnded(UDreamNarrativeDialogueRunner* Runner)
{
	if (ActiveRunner == Runner)
	{
		ActiveRunner = nullptr;
	}
}
