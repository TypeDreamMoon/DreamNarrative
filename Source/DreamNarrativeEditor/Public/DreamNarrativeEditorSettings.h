// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "DreamNarrativeEditorSettings.generated.h"

/** Per-user editor settings: Editor Preferences > Plugins > Dream Narrative. */
UCLASS(config = EditorPerProjectUserSettings, meta = (DisplayName = "Dream Narrative"))
class DREAMNARRATIVEEDITOR_API UDreamNarrativeEditorSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UDreamNarrativeEditorSettings();

	/**
	 * How a node or a diagnostic opens its source. `{file}` and `{line}` are filled in. A link with a
	 * scheme (`vscode://file/{file}:{line}`) is opened as a URL; anything else runs as a command line,
	 * the first word being the program.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Source")
	FString SourceLink;

	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	static const UDreamNarrativeEditorSettings* Get() { return GetDefault<UDreamNarrativeEditorSettings>(); }
};

namespace UE::DreamNarrative::Editor
{
	/** Opens File at Line the way SourceLink says. */
	DREAMNARRATIVEEDITOR_API void OpenSource(const FString& File, int32 Line);

	/** SourceLink with File and Line filled in. */
	DREAMNARRATIVEEDITOR_API FString MakeSourceLink(const FString& Template, const FString& File, int32 Line);
}
