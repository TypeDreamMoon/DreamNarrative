// Copyright DreamMoon. All Rights Reserved.

#include "AbilityTask_PlayNarrativeStage.h"
#include "DreamNarrativeGASBridge.h"
#include "DreamNarrativeGASModule.h"
#include "DreamNarrativeStageDirector.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

UAbilityTask_PlayNarrativeStage* UAbilityTask_PlayNarrativeStage::PlayNarrativeStage(UGameplayAbility* OwningAbility, FName Stage, AActor* Target)
{
	UAbilityTask_PlayNarrativeStage* Task = NewAbilityTask<UAbilityTask_PlayNarrativeStage>(OwningAbility);
	Task->Stage = Stage;
	Task->Target = Target;
	return Task;
}

void UAbilityTask_PlayNarrativeStage::Activate()
{
	AActor* Avatar = GetAvatarActor();
	const UWorld* World = Avatar ? Avatar->GetWorld() : nullptr;
	const UGameInstance* Instance = World ? World->GetGameInstance() : nullptr;
	const UDreamNarrativeGASBridge* Bridge = Instance ? Instance->GetSubsystem<UDreamNarrativeGASBridge>() : nullptr;
	const FDnaStageData* Data = Bridge ? Bridge->FindStage(Stage) : nullptr;
	UDreamNarrativeStageDirector* OnAvatar = Data ? UDreamNarrativeStageDirector::FindOrAddDirector(Avatar) : nullptr;
	if (!OnAvatar)
	{
		UE_LOG(LogDreamNarrativeGAS, Warning, TEXT("Stage %s cannot play for %s: %s"), *Stage.ToString(), *GetNameSafe(Avatar),
			Data ? TEXT("there is no avatar") : TEXT("no registered script declares it"));
		if (ShouldBroadcastAbilityTaskDelegates())
		{
			OnInterrupted.Broadcast(EDnaStageEnd::Stopped);
		}
		EndTask();
		return;
	}
	Director = OnAvatar;
	EndedHandle = OnAvatar->OnPlayEnded.AddUObject(this, &UAbilityTask_PlayNarrativeStage::HandlePlayEnded);
	if (Target.IsValid())
	{
		OnAvatar->BindActor(TEXT("target"), Target.Get());
	}
	PlayNumber = OnAvatar->Play(*Data);
	if (PlayNumber == 0)
	{
		if (ShouldBroadcastAbilityTaskDelegates())
		{
			OnInterrupted.Broadcast(EDnaStageEnd::Stopped);
		}
		EndTask();
	}
}

void UAbilityTask_PlayNarrativeStage::HandlePlayEnded(uint32 Play, EDnaStageEnd Reason)
{
	if (Play != PlayNumber)
	{
		return;
	}
	PlayNumber = 0;
	if (ShouldBroadcastAbilityTaskDelegates())
	{
		(Reason == EDnaStageEnd::Finished ? OnFinished : OnInterrupted).Broadcast(Reason);
	}
	EndTask();
}

void UAbilityTask_PlayNarrativeStage::OnDestroy(bool bInOwnerFinished)
{
	if (UDreamNarrativeStageDirector* OnAvatar = Director.Get())
	{
		OnAvatar->OnPlayEnded.Remove(EndedHandle);
		// The ability is over before its stage: the stage goes with it.
		if (PlayNumber != 0 && OnAvatar->GetPlayNumber() == PlayNumber)
		{
			OnAvatar->Stop(EDnaStageEnd::Stopped);
		}
	}
	PlayNumber = 0;
	Super::OnDestroy(bInOwnerFinished);
}
