// Copyright DreamMoon. All Rights Reserved.

#include "DreamNarrativeEditorModule.h"

#define LOCTEXT_NAMESPACE "FDreamNarrativeEditorModule"

DEFINE_LOG_CATEGORY(LogDreamNarrativeEditor);

void FDreamNarrativeEditorModule::StartupModule()
{
	// Tab spawners, asset type actions and the toolbar entry register here.
}

void FDreamNarrativeEditorModule::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FDreamNarrativeEditorModule, DreamNarrativeEditor)
