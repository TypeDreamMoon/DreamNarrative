// Copyright DreamMoon. All Rights Reserved.
//
// How the story asks for a stage without knowing who plays it. Story and Stage do not depend on each
// other: the Stage module installs a host on the ledger, and a dialogue's `play`, a line's `@` and a
// quest's `on` block go through it. Without a host (Stage not enabled, a test that runs the story
// alone) the story behaves as it always has: the game hears OnPlayStage and plays what it likes.

#pragma once

#include "CoreMinimal.h"
#include "DreamNarrativeStageHost.generated.h"

/** Why a stage stopped playing. */
UENUM(BlueprintType)
enum class EDnaStageEnd : uint8
{
	/** It ran to its end. */
	Finished,
	/** The game, the story or a tool stopped it. */
	Stopped,
	/** Another stage started on the same director. */
	Replaced,
	/** The pawn it was playing for died or was let go. */
	Death,
	/** The world it played in went away: a level change. */
	LevelChange,
	/** The actor it played on was destroyed. */
	OwnerGone,
	/** The watchdog ended it: it ran far past its end, or its director stopped ticking. */
	Watchdog,
};

/** What the story asks of a stage. */
struct FDnaStageRequest
{
	FName Stage;

	/** For a stage attached to a line (`@`): the seconds the line stays up, which a line-relative stage is stretched to. 0 otherwise. */
	double LineSeconds = 0.0;

	/** The channel of the dialogue that asked; None when a quest did. For the log. */
	FName Channel;
};

DECLARE_DELEGATE_OneParam(FDnaStageFinishedDelegate, EDnaStageEnd /*Reason*/);

/** Started and ended stages, for what holds still while one plays. bOnLine: a stage attached to a line. */
DECLARE_MULTICAST_DELEGATE_ThreeParams(FDnaStageActivitySignature, FName /*Stage*/, bool /*bStarted*/, bool /*bOnLine*/);

class DREAMNARRATIVECORE_API IDnaStageHost
{
public:
	virtual ~IDnaStageHost() = default;

	/** A stage the host can play: one a registered script declares. */
	virtual bool HasStage(FName Stage) const = 0;

	/**
	 * Starts a stage. OnFinished runs once when it ends, however it ends, and never inside this call.
	 * Returns a ticket for StopStage; 0 when it could not start, and OnFinished then never runs.
	 */
	virtual uint64 PlayStage(const FDnaStageRequest& Request, FDnaStageFinishedDelegate OnFinished) = 0;

	/** Ends a stage now, putting back what it changed; its OnFinished runs with Stopped. Unknown tickets are ignored. */
	virtual void StopStage(uint64 Ticket) = 0;

	virtual bool IsStagePlaying(uint64 Ticket) const = 0;
};
