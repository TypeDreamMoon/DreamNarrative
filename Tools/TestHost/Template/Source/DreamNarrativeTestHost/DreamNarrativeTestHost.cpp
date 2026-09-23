// Copyright DreamMoon. All Rights Reserved.

#include "DreamNarrativeTestHost.h"
#include "Modules/ModuleManager.h"

// The default implementation on purpose: the host has no behaviour of its own, so anything a test
// observes comes from the plugin under test and not from here.
IMPLEMENT_PRIMARY_GAME_MODULE(FDefaultGameModuleImpl, DreamNarrativeTestHost, "DreamNarrativeTestHost");
