// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DREAMNARRATIVEGAS_API DECLARE_LOG_CATEGORY_EXTERN(LogDreamNarrativeGAS, Log, All);

class FDreamNarrativeGASModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
