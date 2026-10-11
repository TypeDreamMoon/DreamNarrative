// Copyright DreamMoon. All Rights Reserved.
//
// Stable ids. A line or choice without `#id` gets an id from where it stands (`对话.3`, `对话.c2`), and
// that id moves when a line is added above it -- and with it the line's translations, its voice and
// what saves remember of it. Freezing writes each such id into the source, where it then stays.

#pragma once

#include "CoreMinimal.h"

class UDreamNarrativeScript;

namespace UE::DreamNarrative::Lang
{
	class FDnaEditSession;

	/** The ids the compiler made up for lines and choices with no `#id`: by source file, then by line number. */
	DREAMNARRATIVECOMPILER_API void CollectDnaGeneratedIds(const TArray<UDreamNarrativeScript*>& Scripts, TMap<FString, TMap<int32, FString>>& OutBySource);

	/** Writes `#id` at the end of those lines of the session's file, in one edit. OutFrozen counts the lines written. */
	DREAMNARRATIVECOMPILER_API bool FreezeDnaIds(FDnaEditSession& Session, const TMap<int32, FString>& Ids, int32& OutFrozen, FText& OutError);
}
