// Copyright DreamMoon. All Rights Reserved.
//
// From DreamGameplayTask to `.dna` quests. DGT tasks are Blueprint classes whose defaults hold the
// task: its name, title and description, priority, time limit, sub-tasks, and named conditions with
// target counts. Each becomes a quest; each condition a counter fact the game writes (DGT pushed
// counts into conditions, the story watches facts) and an objective on it. Blueprint graphs -- what a
// task did on accept, on completion -- do not carry over; the notes say where to rewrite them as `on`
// blocks. Everything is read by reflection, so the project need not build against DGT.

#pragma once

#include "CoreMinimal.h"

namespace UE::DreamNarrative::Editor
{
	/** The `.dna` text for the given task classes (DGT's UDreamTask subclasses), and notes on what did not carry over. */
	DREAMNARRATIVEEDITOR_API FString MakeDnaFromDreamGameplayTasks(const TArray<const UClass*>& TaskClasses, TArray<FString>& OutNotes);

	/** Every task class the project has, loaded: Blueprint subclasses of the class named `DreamTask`. Empty when DGT is not there. */
	DREAMNARRATIVEEDITOR_API TArray<const UClass*> FindDreamGameplayTaskClasses();

	/** Writes the project's tasks to `<source folder>/Migrated/DreamGameplayTask.dna`; returns the path, empty with OutError on failure. */
	DREAMNARRATIVEEDITOR_API FString MigrateDreamGameplayTasks(TArray<FString>& OutNotes, FText& OutError);
}
