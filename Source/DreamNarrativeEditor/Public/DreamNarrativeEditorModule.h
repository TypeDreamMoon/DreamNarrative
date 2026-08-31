// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DREAMNARRATIVEEDITOR_API DECLARE_LOG_CATEGORY_EXTERN(LogDreamNarrativeEditor, Log, All);

class FDreamNarrativeEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
