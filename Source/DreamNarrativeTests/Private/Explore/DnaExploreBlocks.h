// Copyright DreamMoon. All Rights Reserved.
//
// Runs `explore` blocks: the block's steps first, the way a test block runs them, then the explorer
// from where they leave the story, with the block's depth and properties. Every finding that has a
// way to it comes with a block to paste: a test or explore block that fails the same way.

#pragma once

#include "CoreMinimal.h"
#include "Explore/DnaExplorer.h"

class FAutomationTestBase;

namespace UE::DreamNarrative::Lang
{
	struct FDnaLineNode;
}

namespace UE::DreamNarrative::Tests
{
	class FDnaTestWorld;
}

namespace UE::DreamNarrative::Tests::Explore
{
	struct FDnaExploreBlockResult
	{
		/** The block's own steps ran without a failure (failures are reported through the world). */
		bool bStepsOk = true;
		FDnaExploreStats Stats;
		TArray<FDnaExploreFinding> Findings;
		/** One per finding: the block to paste to see it again, or empty when there is none to give. */
		TArray<FString> Repros;
	};

	void RunDnaExploreBlock(FDnaTestWorld& World, const Lang::FDnaLineNode& Block, const FString& FileLabel, FDnaExploreBlockResult& Out);

	/** Findings become errors (warnings for DNS5106 and DNS5110), each with its repro; the numbers become an info line. */
	void ReportExploreResult(FAutomationTestBase& Test, const FDnaExploreBlockResult& Result);
}
