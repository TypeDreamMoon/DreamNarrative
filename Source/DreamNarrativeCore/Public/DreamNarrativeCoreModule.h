// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DREAMNARRATIVECORE_API DECLARE_LOG_CATEGORY_EXTERN(LogDreamNarrative, Log, All);

namespace DreamNarrative
{
	namespace Names
	{
		/** The plugin every module here ships in. */
		inline const TCHAR* PluginName = TEXT("DreamNarrative");

		/** Extension of a narrative source file. */
		inline const TCHAR* SourceExtension = TEXT("dna");

		/** Directory under the project that narrative sources are looked for in, by convention. */
		inline const TCHAR* DefaultSourceDirectory = TEXT("DNarrative");

		/** Root of the gameplay tag namespace facts are declared under. */
		inline const TCHAR* FactTagRoot = TEXT("Narrative.Fact");
	}

	namespace Diagnostics
	{
		/** Prefix every diagnostic code carries, as in DNS1001. Reserved ranges live in Docs/diagnostics/index.md. */
		inline const TCHAR* CodePrefix = TEXT("DNS");
	}
}

class FDreamNarrativeCoreModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
