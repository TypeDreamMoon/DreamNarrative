// Copyright DreamMoon. All Rights Reserved.
//
// A first pass at staging a dialogue, written into the source for a person to keep, change or roll
// again. Every speaker gets a line-following stage for each one they speak to -- a close-up, an
// over-the-shoulder from behind the one listening, or a medium shot, sometimes with a slow push in --
// attached with `@` to each line they say to them, so the camera cuts between speakers line by line.
// The seed picks the shots; the same seed always gives the same text.

#pragma once

#include "CoreMinimal.h"

namespace UE::DreamNarrative::Lang
{
	class FDnaEditSession;

	/** The prefix of the stages the generator makes for a dialogue: `演出_<对话>_`. */
	DREAMNARRATIVECOMPILER_API FString GetDnaStagingPrefix(const FString& Dialogue);

	/**
	 * Stages a dialogue of the session's file, as one edit. Running it again (another seed: a reroll)
	 * replaces what an earlier run made -- its stages and their `@` -- and leaves lines that carry a
	 * stage of their own alone. False, with OutError, when there is no such dialogue or nobody speaks.
	 */
	DREAMNARRATIVECOMPILER_API bool StageDnaDialogue(FDnaEditSession& Session, const FString& Dialogue, int32 Seed, FText& OutError);
}
