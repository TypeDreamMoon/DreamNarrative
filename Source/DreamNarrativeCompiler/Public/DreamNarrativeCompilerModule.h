// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DREAMNARRATIVECOMPILER_API DECLARE_LOG_CATEGORY_EXTERN(LogDreamNarrativeCompiler, Log, All);

class FDreamNarrativeCompilerModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
