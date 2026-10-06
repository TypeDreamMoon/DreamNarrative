// Copyright DreamMoon. All Rights Reserved.
//
// A level sequence's camera, as a `.dna` stage. Each camera cut becomes a shot where its camera is at
// the start of the cut -- and a second one where it is at the end, blended to over the cut, when the
// camera moves -- with hard cuts between them and the sequence's length as the stage's. Shots are
// placed by an anchor name (default 原点): bind an actor to it to move the whole thing, or leave it
// unbound and the shots stay where they were in the world.

#pragma once

#include "CoreMinimal.h"

class ULevelSequence;
class UWorld;

namespace UE::DreamNarrative::Editor
{
	struct FDnaSequenceImport
	{
		/** The stage's name; empty for the sequence's. */
		FString StageName;

		/** The role the shots are placed by. */
		FString Anchor = TEXT("原点");

		/** Where cameras the sequence possesses (rather than animates) are looked for by label; null for none. */
		UWorld* World = nullptr;
	};

	/** The text of the stage block; false, with OutError, when the sequence has no camera cuts. */
	DREAMNARRATIVEEDITOR_API bool MakeDnaStageFromSequence(ULevelSequence& Sequence, const FDnaSequenceImport& Options, FString& OutText, FText& OutError);

	/** Writes the stage into `<source folder>/Stages/<sequence>.dna`, after what the file has. Returns the file's path; empty on failure. */
	DREAMNARRATIVEEDITOR_API FString ImportDnaStageFromSequence(ULevelSequence& Sequence, const FDnaSequenceImport& Options, FText& OutError);
}
