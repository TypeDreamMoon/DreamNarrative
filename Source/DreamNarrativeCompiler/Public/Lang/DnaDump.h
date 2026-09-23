// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Lang/DnaSyntax.h"

namespace UE::DreamNarrative::Lang
{
	/**
	 * One line per content node: `<line>: <indent><Kind> <detail>`. Blank and comment lines are
	 * skipped. This is what the corpus compares against its `.tree` goldens and what `dnc dump`
	 * prints, so a change to how any line is classified shows up as a diff.
	 */
	DREAMNARRATIVECOMPILER_API FString DumpDnaTree(const FDnaDocument& Document);
}
