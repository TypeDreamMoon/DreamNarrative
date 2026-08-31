// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DREAMNARRATIVESTORY_API DECLARE_LOG_CATEGORY_EXTERN(LogDreamNarrativeStory, Log, All);

class FDreamNarrativeStoryModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
