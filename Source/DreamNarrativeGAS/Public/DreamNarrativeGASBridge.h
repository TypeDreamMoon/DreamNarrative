// Copyright DreamMoon. All Rights Reserved.
//
// The bridge to the Gameplay Ability System: what an ability system goes through becomes what the
// story hears. A gameplay event whose tag a script maps (`event 名 gameplay 标签`) raises that event,
// its parameters taken `from` the payload; a gameplay tag a script maps to an extern fact
// (`fact 名: bool extern gameplay 标签`) keeps the fact in step with the tag's count. Both are written
// as the game. Only the ability systems the game hands over are watched -- usually the player's, and
// a boss's for its phase events.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbilityTypes.h"
#include "DreamNarrativeEvent.h"
#include "GameplayTagContainer.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "DreamNarrativeGASBridge.generated.h"

class UAbilitySystemComponent;
class UDreamNarrativeLedger;
class UDreamNarrativeScript;

UCLASS()
class DREAMNARRATIVEGAS_API UDreamNarrativeGASBridge : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** For tools and tests that have no game instance: wires the bridge to a ledger by hand. */
	void InitializeStandalone(UDreamNarrativeLedger* InLedger);

	/**
	 * Hears the gameplay events AbilitySystem receives that a script maps to an event; with
	 * bMirrorTags, its gameplay tags also feed the facts mapped to them, starting with the counts it
	 * has now. Watching it again only changes bMirrorTags. Scripts registered later are picked up.
	 */
	UFUNCTION(BlueprintCallable, Category = "Narrative|GAS")
	void WatchAbilitySystem(UAbilitySystemComponent* AbilitySystem, bool bMirrorTags = true);

	UFUNCTION(BlueprintCallable, Category = "Narrative|GAS")
	void UnwatchAbilitySystem(UAbilitySystemComponent* AbilitySystem);

	/**
	 * Raises every event a script maps EventTag (or a tag above it) to, its parameters taken from
	 * Payload. False when no script maps it. For gameplay events the game routes itself.
	 */
	UFUNCTION(BlueprintCallable, Category = "Narrative|GAS")
	bool RaiseFromGameplayEvent(FGameplayTag EventTag, const FGameplayEventData& Payload);

	/** The narrative event Decl makes of a gameplay event: each parameter mapped `from` the payload, the rest left to their defaults. */
	static FDnaEventPayload MakeEvent(const FDnaEventDecl& Decl, const FGameplayEventData& Payload);

private:
	struct FWatch
	{
		TWeakObjectPtr<UAbilitySystemComponent> System;
		bool bMirrorTags = false;
		/** One per mapped event: the tag it listens for and its delegate. */
		TArray<TPair<FGameplayTagContainer, FDelegateHandle>> Events;
		/** One per mirrored fact. */
		TArray<TPair<FGameplayTag, FDelegateHandle>> Tags;
	};

	void Bind(FWatch& Watch);
	void Unbind(FWatch& Watch);
	void HandleScriptRegistered(UDreamNarrativeScript* Script);
	void HandleGameplayEvent(FGameplayTag MatchingTag, const FGameplayEventData* Payload, FName Event);
	void HandleTagCount(const FGameplayTag Tag, int32 Count, FName Fact);
	void WriteTagCount(FName Fact, int32 Count);
	/** The tag a script names; an invalid one, warned about once, when the project has no such tag. */
	FGameplayTag ResolveTag(FName Name);

	UPROPERTY()
	TObjectPtr<UDreamNarrativeLedger> Ledger;

	TArray<FWatch> Watches;
	FDelegateHandle ScriptHandle;
	TSet<FName> WarnedTags;
};
