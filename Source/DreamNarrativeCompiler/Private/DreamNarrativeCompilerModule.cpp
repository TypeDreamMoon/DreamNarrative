// Copyright DreamMoon. All Rights Reserved.

#include "DreamNarrativeCompilerModule.h"

#define LOCTEXT_NAMESPACE "FDreamNarrativeCompilerModule"

DEFINE_LOG_CATEGORY(LogDreamNarrativeCompiler);

void FDreamNarrativeCompilerModule::StartupModule()
{
	// The source watcher starts here once it exists. Two editors open on the same project must not
	// both rebuild the same asset - the watcher elects a single owner before it touches disk.
}

void FDreamNarrativeCompilerModule::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FDreamNarrativeCompilerModule, DreamNarrativeCompiler)
