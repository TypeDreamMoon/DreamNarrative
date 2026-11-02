// Copyright DreamMoon. All Rights Reserved.
//
// Stable ids. A line or choice without `#id` gets an id from where it stands (`对话.3`, `对话.c2`), and
// that id moves when a line is added above it -- and with it the line's translations, its voice and
// what saves remember of it. An objective's text, an archive entry and a stage's text are numbered the
// same way (`objective2`, `entry0`, `text1`) for their translation keys. Freezing writes each such id
// into the source, where it then stays.

#pragma once

#include "CoreMinimal.h"
#include "Lang/DnaLocalization.h"

namespace UE::DreamNarrative::Lang
{
	class FDnaEditSession;

	/** The ids the compiler numbered texts by where they had no `#id`, from what it gave to translate: by source file, then by line number. */
	DREAMNARRATIVECOMPILER_API void CollectDnaGeneratedIds(const TArray<FDnaLocManifest>& Manifests, TMap<FString, TMap<int32, FString>>& OutBySource);

	/** Writes `#id` at the end of those lines of the session's file, in one edit. OutFrozen counts the lines written. */
	DREAMNARRATIVECOMPILER_API bool FreezeDnaIds(FDnaEditSession& Session, const TMap<int32, FString>& Ids, int32& OutFrozen, FText& OutError);
}
