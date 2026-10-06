// Copyright DreamMoon. All Rights Reserved.
//
// An ability that plays a stage and ends with it, the way a finisher would: what the smoke tests
// grant and activate to see the ability task at work.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "AbilityTask_PlayNarrativeStage.h"
#include "DnaTestStageAbility.generated.h"

UCLASS()
class UDnaTestStageAbility : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UDnaTestStageAbility()
	{
		InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
		NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
	}

	/** The stage it plays, and the actor bound as its `target`. */
	static FName StageToPlay;
	static TWeakObjectPtr<AActor> StageTarget;
	/** How the last stage it played ended, and how many have. */
	static EDnaStageEnd LastEnd;
	static int32 Ends;

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle SpecHandle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override
	{
		UAbilityTask_PlayNarrativeStage* Task = UAbilityTask_PlayNarrativeStage::PlayNarrativeStage(this, StageToPlay, StageTarget.Get());
		Task->OnFinished.AddDynamic(this, &UDnaTestStageAbility::HandleEnded);
		Task->OnInterrupted.AddDynamic(this, &UDnaTestStageAbility::HandleEnded);
		Task->ReadyForActivation();
	}

private:
	UFUNCTION()
	void HandleEnded(EDnaStageEnd Reason)
	{
		LastEnd = Reason;
		++Ends;
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, /*bReplicateEndAbility=*/true, /*bWasCancelled=*/Reason != EDnaStageEnd::Finished);
	}
};
