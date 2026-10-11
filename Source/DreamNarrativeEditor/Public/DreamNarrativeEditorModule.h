// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DREAMNARRATIVEEDITOR_API DECLARE_LOG_CATEGORY_EXTERN(LogDreamNarrativeEditor, Log, All);

namespace UE::DreamNarrative::Editor
{
	class FDnaSourceWatcher;

	/**
	 * The packages of every narrative script under the asset directory. A game finds its scripts by
	 * listing that directory, so nothing refers to them: a cook adds these itself, and the assets
	 * their stages play come along as the scripts' soft references.
	 */
	DREAMNARRATIVEEDITOR_API void CollectScriptPackagesToCook(TArray<FName>& OutPackages);
}

class FDreamNarrativeEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	TUniquePtr<UE::DreamNarrative::Editor::FDnaSourceWatcher> Watcher;
	FDelegateHandle CookHandle;
};
