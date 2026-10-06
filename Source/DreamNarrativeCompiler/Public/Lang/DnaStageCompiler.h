// Copyright DreamMoon. All Rights Reserved.
//
// A `stage` block read into what the director plays, and checked on the way. The validator runs it
// for the diagnostics and the generator for the data, so the two never disagree about what a line
// means.

#pragma once

#include "CoreMinimal.h"
#include "DreamNarrativeStageData.h"
#include "Lang/DnaDiagnostic.h"
#include "Lang/DnaSyntax.h"

namespace UE::DreamNarrative::Lang
{
	/**
	 * Reads a stage block. Texts (`ui text`) come out as plain strings; the generator makes them
	 * localisable. Returns false when any error was recorded.
	 *
	 *   DNS7001  a cut that no mask is at full over
	 *   DNS7002  an entry past the stage's end
	 *   DNS7003  a change to the world that nothing puts back (a taken camera, a hidden HUD, a text, dimmed lights, a key light)
	 *   DNS7004  a time in the wrong unit: seconds or `duration` in a line stage, a percentage in an absolute one
	 *   DNS7005  a range that ends before it starts
	 *   DNS7006  a verb the track does not have, arguments it does not take, an entry `restore` cannot hold, an unknown property
	 *   DNS7007  a verb that takes a range written at a point, or one that happens at a point written as a range
	 *   DNS7008  a shot the stage does not declare
	 *   DNS7009  an absolute stage without a duration
	 */
	DREAMNARRATIVECOMPILER_API bool CompileDnaStage(const FDnaLineNode& Root, FDnaStageData& Out, FDnaDiagnosticSink& Diagnostics);
}
