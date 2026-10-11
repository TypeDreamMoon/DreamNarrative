// Copyright DreamMoon. All Rights Reserved.
//
// An ability plays a stage and waits for it: a finisher's camera, hitstop and flashes written in a
// `.dna` stage, played on the ability's avatar. The task ends when the stage does; ending the ability
// first stops the stage and puts the world back.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/Tasks/AbilityTask.h"
#include "DreamNarrativeStageHost.h"
#include "AbilityTask_PlayNarrativeStage.generated.h"

class UDreamNarrativeStageDirector;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDnaStageTaskSignature, EDnaStageEnd, Reason);

UCLASS()
class DREAMNARRATIVEGAS_API UAbilityTask_PlayNarrativeStage : public UAbilityTask
{
	GENERATED_BODY()

public:
	/** The stage ran to its end. */
	UPROPERTY(BlueprintAssignable)
	FDnaStageTaskSignature OnFinished;

	/** The stage ended early (stopped, replaced, the avatar dead, the level gone), or could not play. */
	UPROPERTY(BlueprintAssignable)
	FDnaStageTaskSignature OnInterrupted;

	/**
	 * Plays Stage (a stage a registered script declares) on the ability's avatar. Target, when given,
	 * is bound as the stage's `target`.
	 */
	UFUNCTION(BlueprintCallable, Category = "Ability|Tasks", meta = (HidePin = "OwningAbility", DefaultToSelf = "OwningAbility", BlueprintInternalUseOnly = "TRUE"))
	static UAbilityTask_PlayNarrativeStage* PlayNarrativeStage(UGameplayAbility* OwningAbility, FName Stage, AActor* Target = nullptr);

	virtual void Activate() override;

protected:
	virtual void OnDestroy(bool bInOwnerFinished) override;

private:
	void HandlePlayEnded(uint32 Play, EDnaStageEnd Reason);

	FName Stage;
	TWeakObjectPtr<AActor> Target;
	TWeakObjectPtr<UDreamNarrativeStageDirector> Director;
	uint32 PlayNumber = 0;
	bool bStarting = false;
	bool bDestroyed = false;
	TMap<uint32, EDnaStageEnd> StartupEnds;
	FDelegateHandle EndedHandle;
};
