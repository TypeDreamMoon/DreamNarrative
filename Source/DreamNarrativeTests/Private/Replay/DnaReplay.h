// Copyright DreamMoon. All Rights Reserved.
//
// Replaying recordings, and the project's own tests. A project keeps its tests beside its story, under
// the content root's Tests/ (recordings under Tests/Recorded/), and they compile with the whole content
// root as one program. When a recording no longer plays the way it was recorded, it is played again
// with a recorder attached: what that writes is the test as the story plays now, the `.actual` to put
// in its place when the change was meant.

#pragma once

#include "CoreMinimal.h"

namespace UE::DreamNarrative::Lang
{
	struct FDnaLineNode;
}

namespace UE::DreamNarrative::Tests
{
	class FDnaTestWorld;
}

namespace UE::DreamNarrative::Tests::Replay
{
	/** <project>/<source directory>, absolute; empty when the project has no narrative sources. */
	FString GetContentRoot();

	/** The content root's test files, relative to it with forward slashes, sorted. */
	TArray<FString> FindProjectTestFiles();

	/** Compiles every .dna under the content root as one program into World. */
	bool CompileContentRoot(FDnaTestWorld& World);

	/** Runs one test block of the world's program, handing back its failures and what it logged instead of reporting them. */
	TArray<FString> RunCollecting(FDnaTestWorld& World, const FString& BlockName, const FString& FileLabel);

	/**
	 * Plays a test block again with a recorder that starts at its first move (after the steps that
	 * only set up), and returns what the recorder wrote: the test as the story plays now. Failures of
	 * the replay itself are not reported.
	 */
	FString Rerecord(FDnaTestWorld& World, const Lang::FDnaLineNode& TestNode, const FString& FileLabel);

	/**
	 * The lines that differ between two texts, with a line of context around each change: "- " only
	 * in Before, "+ " only in After, "  " in both, "  ..." for what is left out. Empty when they match.
	 */
	TArray<FString> DiffLines(const FString& Before, const FString& After);
}
