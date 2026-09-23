// Copyright DreamMoon. All Rights Reserved.
//
// Carries out `test` blocks: the steps a .dna test writes (syntax proposal v0.3 §15), on an
// FDnaTestWorld that compiled the program the block belongs to.

#pragma once

#include "CoreMinimal.h"

namespace UE::DreamNarrative::Lang
{
	struct FDnaLineNode;
}

namespace UE::DreamNarrative::Tests
{
	class FDnaTestWorld;

	/**
	 * Runs one `test` block of the world's program, step by step. Every failure is reported with
	 * FileLabel and the line it concerns, e.g. `Samples/Ring.dna(14): ...`, and the run goes on to the
	 * next step, so one run shows every broken step. Returns false when any step failed.
	 */
	bool RunDnaTestBlock(FDnaTestWorld& World, const Lang::FDnaLineNode& TestNode, const FString& FileLabel);
}
