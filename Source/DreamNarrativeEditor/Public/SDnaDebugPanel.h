// Copyright DreamMoon. All Rights Reserved.
//
// The runtime debug panel: the story of the game running in the editor (PIE), as it stands --
// the chapter, its checkpoints, every fact with its value and where it is kept, what each channel is
// saying, and who wrote which fact last. From here the chapter can be jumped to or selected, a new
// playthrough started, and each channel's dialogue stepped on: what the `dn.` commands do, with eyes.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class UDreamNarrativeStory;
class SVerticalBox;
class STextBlock;

class DREAMNARRATIVEEDITOR_API SDnaDebugPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDnaDebugPanel) {}
		/** The story to show; null for the one of the game running in the editor. */
		SLATE_ARGUMENT(UDreamNarrativeStory*, Story)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SDnaDebugPanel() override;

	/** Reads the story again; the panel does this four times a second on its own. */
	void Refresh();

	int32 NumFactRows() const { return FactRows.Num(); }
	int32 NumWrites() const { return Writes.Num(); }
	const TArray<TSharedPtr<FString>>& GetFactRows() const { return FactRows; }

private:
	UDreamNarrativeStory* FindStory() const;
	void Bind(UDreamNarrativeStory* Story);
	void Unbind();
	void RebuildChannels(UDreamNarrativeStory& Story);
	TSharedRef<ITableRow> MakeTextRow(TSharedPtr<FString> Item, const TSharedRef<STableViewBase>& Owner);

	TWeakObjectPtr<UDreamNarrativeStory> FixedStory;
	TWeakObjectPtr<UDreamNarrativeStory> BoundStory;
	FDelegateHandle WriteHandle;

	TArray<TSharedPtr<FString>> FactRows;
	TArray<TSharedPtr<FString>> Writes;
	FString ChannelsShown;
	TSharedPtr<SListView<TSharedPtr<FString>>> FactList;
	TSharedPtr<SListView<TSharedPtr<FString>>> WriteList;
	TSharedPtr<SVerticalBox> ChannelBox;
	TSharedPtr<STextBlock> Header;
	TSharedPtr<class SEditableTextBox> ChapterBox;
};
