// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DREAMNARRATIVESTAGE_API DECLARE_LOG_CATEGORY_EXTERN(LogDreamNarrativeStage, Log, All);

class FDreamNarrativeStageModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
