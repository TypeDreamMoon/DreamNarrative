// Copyright DreamMoon. All Rights Reserved.

#include "DreamNarrativeEditorModule.h"
#include "DnaSourceWatcher.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "SDnaDebugPanel.h"
#include "SDnaStageTimeline.h"
#include "SDnaStoryPanel.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "FDreamNarrativeEditorModule"

DEFINE_LOG_CATEGORY(LogDreamNarrativeEditor);

namespace
{
	const FName StoryTab(TEXT("DreamNarrativeStory"));
	const FName DebugTab(TEXT("DreamNarrativeDebug"));
	const FName TimelineTab(TEXT("DreamNarrativeStageTimeline"));
}

void FDreamNarrativeEditorModule::StartupModule()
{
	// Saving a .dna rebuilds the scripts (not in commandlets and unattended runs).
	Watcher = MakeUnique<UE::DreamNarrative::Editor::FDnaSourceWatcher>();
	Watcher->Start();

	// A commandlet has no windows to put a tab in.
	if (!FSlateApplication::IsInitialized())
	{
		return;
	}
	const TSharedRef<FWorkspaceItem> Tools = WorkspaceMenu::GetMenuStructure().GetToolsCategory();
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(StoryTab, FOnSpawnTab::CreateLambda([](const FSpawnTabArgs&)
	{
		return SNew(SDockTab).TabRole(ETabRole::NomadTab)[SNew(SDnaStoryPanel)];
	}))
	.SetDisplayName(LOCTEXT("StoryTab", "Narrative Story"))
	.SetTooltipText(LOCTEXT("StoryTabTip", "The project's chapters, quests, dialogues and what leads where; edits go into the .dna sources"))
	.SetGroup(Tools);

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(DebugTab, FOnSpawnTab::CreateLambda([](const FSpawnTabArgs&)
	{
		return SNew(SDockTab).TabRole(ETabRole::NomadTab)[SNew(SDnaDebugPanel)];
	}))
	.SetDisplayName(LOCTEXT("DebugTab", "Narrative Debugger"))
	.SetTooltipText(LOCTEXT("DebugTabTip", "Facts, chapter, checkpoints and channels of the story running in Play In Editor"))
	.SetGroup(Tools);

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(TimelineTab, FOnSpawnTab::CreateLambda([](const FSpawnTabArgs&)
	{
		return SNew(SDockTab).TabRole(ETabRole::NomadTab)[SNew(SDnaStageTimeline)];
	}))
	.SetDisplayName(LOCTEXT("TimelineTab", "Narrative Stage Timeline"))
	.SetTooltipText(LOCTEXT("TimelineTabTip", "A stage's tracks and entries; dragging an entry rewrites its line in the .dna source"))
	.SetGroup(Tools);
}

void FDreamNarrativeEditorModule::ShutdownModule()
{
	Watcher.Reset();
	if (FSlateApplication::IsInitialized())
	{
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(StoryTab);
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(DebugTab);
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TimelineTab);
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FDreamNarrativeEditorModule, DreamNarrativeEditor)
