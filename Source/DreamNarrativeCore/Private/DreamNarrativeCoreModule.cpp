// Copyright DreamMoon. All Rights Reserved.

#include "DreamNarrativeCoreModule.h"

#define LOCTEXT_NAMESPACE "FDreamNarrativeCoreModule"

DEFINE_LOG_CATEGORY(LogDreamNarrative);

void FDreamNarrativeCoreModule::StartupModule()
{
	// Nothing to register yet. Fact tag registration from compiled assets lands here.
}

void FDreamNarrativeCoreModule::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FDreamNarrativeCoreModule, DreamNarrativeCore)
