// Copyright DreamMoon. All Rights Reserved.

#include "DreamNarrativeGASBridge.h"
#include "AbilitySystemComponent.h"
#include "DreamNarrativeGASModule.h"
#include "DreamNarrativeLedger.h"
#include "DreamNarrativeStageData.h"
#include "DreamNarrativeStageDirector.h"
#include "DreamNarrativeStory.h"

void UDreamNarrativeGASBridge::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UDreamNarrativeLedger* WithLedger = Collection.InitializeDependency<UDreamNarrativeLedger>();
	InitializeStandalone(WithLedger, Collection.InitializeDependency<UDreamNarrativeStory>());
}

void UDreamNarrativeGASBridge::InitializeStandalone(UDreamNarrativeLedger* InLedger, UDreamNarrativeStory* InStory)
{
	Ledger = InLedger;
	Story = InStory;
	if (Ledger)
	{
		ScriptHandle = Ledger->OnScriptRegistered.AddUObject(this, &UDreamNarrativeGASBridge::HandleScriptRegistered);
	}
	if (Story)
	{
		Story->OnQuestStateChanged.AddDynamic(this, &UDreamNarrativeGASBridge::HandleQuestStateChanged);
	}
}

void UDreamNarrativeGASBridge::Deinitialize()
{
	for (FWatch& Watch : Watches)
	{
		Unbind(Watch);
	}
	Watches.Reset();
	if (Ledger)
	{
		Ledger->OnScriptRegistered.Remove(ScriptHandle);
	}
	if (Story)
	{
		Story->OnQuestStateChanged.RemoveDynamic(this, &UDreamNarrativeGASBridge::HandleQuestStateChanged);
	}
	ScriptHandle.Reset();
	Super::Deinitialize();
}

// ---------------------------------------------------------------- watching

void UDreamNarrativeGASBridge::WatchAbilitySystem(UAbilitySystemComponent* AbilitySystem, bool bMirrorTags)
{
	if (!AbilitySystem)
	{
		return;
	}
	for (FWatch& Watch : Watches)
	{
		if (Watch.System == AbilitySystem)
		{
			Unbind(Watch);
			Watch.bMirrorTags = bMirrorTags;
			Bind(Watch);
			return;
		}
	}
	FWatch& Watch = Watches.AddDefaulted_GetRef();
	Watch.System = AbilitySystem;
	Watch.bMirrorTags = bMirrorTags;
	Bind(Watch);
}

void UDreamNarrativeGASBridge::UnwatchAbilitySystem(UAbilitySystemComponent* AbilitySystem)
{
	const int32 Index = Watches.IndexOfByPredicate([AbilitySystem](const FWatch& Watch) { return Watch.System == AbilitySystem; });
	if (Index != INDEX_NONE)
	{
		Unbind(Watches[Index]);
		Watches.RemoveAt(Index);
	}
}

void UDreamNarrativeGASBridge::Bind(FWatch& Watch)
{
	UAbilitySystemComponent* System = Watch.System.Get();
	if (!System || !Ledger)
	{
		return;
	}
	TArray<FName> Events;
	Ledger->GetEventNames(Events);
	Events.Sort(FNameLexicalLess());
	for (const FName& Event : Events)
	{
		const FDnaEventDecl* Decl = Ledger->FindEventDecl(Event);
		const FGameplayTag Tag = Decl ? ResolveTag(Decl->GameplayTag) : FGameplayTag();
		if (!Tag.IsValid())
		{
			continue;
		}
		const FGameplayTagContainer Listens(Tag);
		const FDelegateHandle Handle = System->AddGameplayEventTagContainerDelegate(Listens,
			FGameplayEventTagMulticastDelegate::FDelegate::CreateUObject(this, &UDreamNarrativeGASBridge::HandleGameplayEvent, Event));
		Watch.Events.Emplace(Listens, Handle);
	}
	// Stages an ability system's gameplay events play.
	for (const UDreamNarrativeScript* Script : Ledger->GetScripts())
	{
		const UDnaStageSection* Section = Script ? Script->FindSection<UDnaStageSection>() : nullptr;
		for (const FDnaStageData& Stage : Section ? Section->Stages : TArray<FDnaStageData>())
		{
			const FGameplayTag Tag = ResolveTag(Stage.GameplayTag);
			if (!Tag.IsValid())
			{
				continue;
			}
			const FGameplayTagContainer Listens(Tag);
			const FDelegateHandle Handle = System->AddGameplayEventTagContainerDelegate(Listens,
				FGameplayEventTagMulticastDelegate::FDelegate::CreateUObject(this, &UDreamNarrativeGASBridge::HandleStageEvent, Stage.Name, TWeakObjectPtr<UAbilitySystemComponent>(System)));
			Watch.Events.Emplace(Listens, Handle);
		}
	}
	if (!Watch.bMirrorTags)
	{
		return;
	}
	TArray<FName> Facts;
	Ledger->GetFactNames(Facts);
	Facts.Sort(FNameLexicalLess());
	for (const FName& Fact : Facts)
	{
		const FDnaFactDecl* Decl = Ledger->FindFactDecl(Fact);
		const FGameplayTag Tag = Decl && Decl->Owner == EDnaFactOwner::Extern ? ResolveTag(Decl->GameplayTag) : FGameplayTag();
		if (!Tag.IsValid())
		{
			continue;
		}
		const FDelegateHandle Handle = System->RegisterGameplayTagEvent(Tag, EGameplayTagEventType::AnyCountChange)
			.AddUObject(this, &UDreamNarrativeGASBridge::HandleTagCount, Fact);
		Watch.Tags.Emplace(Tag, Handle);
		WriteTagCount(Fact, System->GetTagCount(Tag));
	}
}

void UDreamNarrativeGASBridge::Unbind(FWatch& Watch)
{
	if (UAbilitySystemComponent* System = Watch.System.Get())
	{
		for (const TPair<FGameplayTagContainer, FDelegateHandle>& Event : Watch.Events)
		{
			System->RemoveGameplayEventTagContainerDelegate(Event.Key, Event.Value);
		}
		for (const TPair<FGameplayTag, FDelegateHandle>& Tag : Watch.Tags)
		{
			System->RegisterGameplayTagEvent(Tag.Key, EGameplayTagEventType::AnyCountChange).Remove(Tag.Value);
		}
	}
	Watch.Events.Reset();
	Watch.Tags.Reset();
}

void UDreamNarrativeGASBridge::HandleScriptRegistered(UDreamNarrativeScript* Script)
{
	// New declarations may map more: bind everything again.
	for (FWatch& Watch : Watches)
	{
		Unbind(Watch);
		Bind(Watch);
	}
}

FGameplayTag UDreamNarrativeGASBridge::ResolveTag(FName Name)
{
	if (Name.IsNone())
	{
		return FGameplayTag();
	}
	const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(Name, /*ErrorIfNotFound=*/false);
	if (!Tag.IsValid() && !WarnedTags.Contains(Name))
	{
		WarnedTags.Add(Name);
		UE_LOG(LogDreamNarrativeGAS, Warning, TEXT("A script maps the gameplay tag '%s', which the project does not register; nothing is heard from it"), *Name.ToString());
	}
	return Tag;
}

// ---------------------------------------------------------------- what comes in

void UDreamNarrativeGASBridge::HandleGameplayEvent(FGameplayTag MatchingTag, const FGameplayEventData* Payload, FName Event)
{
	const FDnaEventDecl* Decl = Ledger ? Ledger->FindEventDecl(Event) : nullptr;
	if (Decl)
	{
		Ledger->RaiseEvent(MakeEvent(*Decl, Payload ? *Payload : FGameplayEventData()));
	}
}

void UDreamNarrativeGASBridge::HandleStageEvent(FGameplayTag MatchingTag, const FGameplayEventData* Payload, FName Stage, TWeakObjectPtr<UAbilitySystemComponent> System)
{
	PlayStageFor(System.Get(), Stage, Payload);
}

const FDnaStageData* UDreamNarrativeGASBridge::FindStage(FName Stage) const
{
	if (!Ledger)
	{
		return nullptr;
	}
	for (const UDreamNarrativeScript* Script : Ledger->GetScripts())
	{
		const UDnaStageSection* Section = Script ? Script->FindSection<UDnaStageSection>() : nullptr;
		if (const FDnaStageData* Found = Section ? Section->FindStage(Stage) : nullptr)
		{
			return Found;
		}
	}
	return nullptr;
}

bool UDreamNarrativeGASBridge::PlayStageFor(UAbilitySystemComponent* AbilitySystem, FName Stage, const FGameplayEventData* Payload)
{
	const FDnaStageData* Data = FindStage(Stage);
	AActor* Avatar = AbilitySystem ? AbilitySystem->GetAvatarActor() : nullptr;
	UDreamNarrativeStageDirector* Director = UDreamNarrativeStageDirector::FindOrAddDirector(Avatar);
	if (!Data || !Director)
	{
		UE_LOG(LogDreamNarrativeGAS, Warning, TEXT("Stage %s cannot play for %s: %s"), *Stage.ToString(), *GetNameSafe(Avatar),
			Data ? TEXT("there is no avatar to play it on") : TEXT("no registered script declares it"));
		return false;
	}
	Director->BindActor(TEXT("target"), Payload ? const_cast<AActor*>(Payload->Target.Get()) : nullptr);
	Director->BindActor(TEXT("instigator"), Payload ? const_cast<AActor*>(Payload->Instigator.Get()) : nullptr);
	return Director->Play(*Data) != 0;
}

void UDreamNarrativeGASBridge::HandleQuestStateChanged(FName Quest, EDnaQuestState OldState, EDnaQuestState NewState)
{
	if (NewState != EDnaQuestState::Completed || !Story)
	{
		return;
	}
	const FDnaQuestData* Data = Story->FindQuest(Quest);
	const FGameplayTag Tag = Data ? ResolveTag(Data->GameplayTag) : FGameplayTag();
	if (!Tag.IsValid())
	{
		return;
	}
	for (const FWatch& Watch : TArray<FWatch>(Watches))
	{
		if (UAbilitySystemComponent* System = Watch.System.Get())
		{
			FGameplayEventData Payload;
			Payload.EventTag = Tag;
			System->HandleGameplayEvent(Tag, &Payload);
		}
	}
}

bool UDreamNarrativeGASBridge::RaiseFromGameplayEvent(FGameplayTag EventTag, const FGameplayEventData& Payload)
{
	if (!Ledger || !EventTag.IsValid())
	{
		return false;
	}
	TArray<FName> Events;
	Ledger->GetEventNames(Events);
	Events.Sort(FNameLexicalLess());
	bool bRaised = false;
	for (const FName& Event : Events)
	{
		const FDnaEventDecl* Decl = Ledger->FindEventDecl(Event);
		const FGameplayTag Tag = Decl ? ResolveTag(Decl->GameplayTag) : FGameplayTag();
		if (Tag.IsValid() && EventTag.MatchesTag(Tag))
		{
			Ledger->RaiseEvent(MakeEvent(*Decl, Payload));
			bRaised = true;
		}
	}
	return bRaised;
}

FDnaEventPayload UDreamNarrativeGASBridge::MakeEvent(const FDnaEventDecl& Decl, const FGameplayEventData& Payload)
{
	FDnaEventPayload Event;
	Event.Event = Decl.Name;
	for (int32 Index = 0; Index < Decl.Params.Num() && Index < Decl.ParamSources.Num(); ++Index)
	{
		const FDnaFactDecl& Param = Decl.Params[Index];
		const FDnaEventParamSource& From = Decl.ParamSources[Index];
		if (From.Source == EDnaGameplaySource::Magnitude)
		{
			Event.Set(Param.Name, FDnaValue::FromFloat(Payload.EventMagnitude));
			continue;
		}
		if (From.Source != EDnaGameplaySource::InstigatorTags && From.Source != EDnaGameplaySource::TargetTags)
		{
			continue;
		}
		const FGameplayTagContainer& Tags = From.Source == EDnaGameplaySource::InstigatorTags ? Payload.InstigatorTags : Payload.TargetTags;
		if (Param.Type == EDnaFactType::Bool)
		{
			const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(From.Tag, /*ErrorIfNotFound=*/false);
			Event.Set(Param.Name, FDnaValue::FromBool(Tag.IsValid() && Tags.HasTag(Tag)));
			continue;
		}
		// A tag parameter: the member named like the child of From.Tag the tags have (`敌人.体型.大型` gives 大型).
		const FString Prefix = From.Tag.ToString() + TEXT(".");
		for (const FGameplayTag& Have : Tags)
		{
			const FString Name = Have.ToString();
			const FString Leaf = Name.StartsWith(Prefix) ? Name.Mid(Prefix.Len()) : FString();
			if (!Leaf.IsEmpty() && !Leaf.Contains(TEXT(".")) && Param.Domain.Contains(FName(*Leaf)))
			{
				Event.Set(Param.Name, FDnaValue::FromTag(FName(*Leaf)));
				break;
			}
		}
	}
	return Event;
}

void UDreamNarrativeGASBridge::HandleTagCount(const FGameplayTag Tag, int32 Count, FName Fact)
{
	WriteTagCount(Fact, Count);
}

void UDreamNarrativeGASBridge::WriteTagCount(FName Fact, int32 Count)
{
	const FDnaFactDecl* Decl = Ledger ? Ledger->FindFactDecl(Fact) : nullptr;
	if (!Decl)
	{
		return;
	}
	const FDnaValue Value = Decl->Type == EDnaFactType::Int ? FDnaValue::FromInt(Count) : FDnaValue::FromBool(Count > 0);
	if (!Ledger->GetFact(Fact).Equals(Value))
	{
		Ledger->SetFact(Fact, Value, EDnaWriteSource::Game);
	}
}
