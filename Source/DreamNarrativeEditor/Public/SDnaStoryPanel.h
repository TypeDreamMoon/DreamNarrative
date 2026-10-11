// Copyright DreamMoon. All Rights Reserved.
//
// The story view: the project's chapters, quests, dialogues, events, stages and archives with what
// leads where, read off the sources the build reads. Double-click goes to the line in the source.
//
// Edits go into the text through an editing session per file -- a condition rewritten, two dialogues
// connected, a dialogue added or removed -- each changing only its own lines; undo and redo are the
// sessions' own, and nothing reaches the disk until Save.

#pragma once

#include "CoreMinimal.h"
#include "Graph/DnaStoryGraph.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STreeView.h"

namespace UE::DreamNarrative::Lang
{
	class FDnaEditSession;
}

class DREAMNARRATIVEEDITOR_API SDnaStoryPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDnaStoryPanel) {}
		/** The sources to show; empty for the project's. */
		SLATE_ARGUMENT(TArray<FString>, Files)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Reads the sources again (unsaved edits included) and rebuilds the view. */
	void Reload();

	const UE::DreamNarrative::Graph::FDnaStoryGraph& GetGraph() const { return Graph; }
	int32 GetErrorCount() const { return ErrorCount; }
	int32 GetGroupCount() const { return Roots.Num(); }

	// -------------------------------------------------------------- edits

	bool EditCondition(const FString& Dialogue, const FString& Condition, FText& OutError);
	bool Connect(const FString& From, const FString& To, FText& OutError);
	bool AddDialogue(const FString& File, const FString& Name, const FString& Clauses, const FString& FirstLine, FText& OutError);
	bool RemoveDialogue(const FString& Dialogue, FText& OutError);
	/** Generates a staging for a dialogue (each run a new roll of the shots, replacing the last). */
	bool StageDialogue(const FString& Dialogue, FText& OutError);
	bool Undo();
	bool Redo();
	/** Writes every file with unsaved edits. False with the reasons when one could not be written. */
	bool SaveAll(TArray<FString>& OutErrors);
	bool HasUnsavedEdits() const;
	/** Files with unsaved edits whose text on disk changed since the panel read or saved them. */
	TArray<FString> GetConflictedFiles() const;
	/** Drops a file's unsaved edits, and their undo steps, and reads it from disk again. */
	bool DiscardEdits(const FString& File, FText& OutError);
	/** Writes a file's edits over the change made on disk. */
	bool OverwriteDisk(const FString& File, FText& OutError);

private:
	struct FItem
	{
		FText Label;
		FText Detail;
		FString File;
		int32 Line = 0;
		/** The graph node it stands for; INDEX_NONE for a group or an edge. */
		int32 Node = INDEX_NONE;
		TArray<TSharedPtr<FItem>> Children;
	};
	using FItemPtr = TSharedPtr<FItem>;

	UE::DreamNarrative::Lang::FDnaEditSession& SessionFor(const FString& File);
	/** The file a dialogue is declared in; empty when the graph has none. */
	FString FileOf(const FString& Dialogue) const;
	bool Edited(const FString& File, bool bOk);
	void Rebuild();

	TSharedRef<ITableRow> MakeRow(FItemPtr Item, const TSharedRef<STableViewBase>& Owner);
	void GetItemChildren(FItemPtr Item, TArray<FItemPtr>& OutChildren);
	void OnOpen(FItemPtr Item);
	TSharedPtr<SWidget> MakeContextMenu();
	void Report(const FText& Error);
	/** After a refused save: asks what to do with the files changed on disk, and does it. */
	void ResolveConflicts(TArray<FString>& Errors);

	TArray<FString> Files;
	bool bProjectFiles = true;
	TMap<FString, TSharedPtr<UE::DreamNarrative::Lang::FDnaEditSession>> Sessions;
	/** How many times each dialogue has been staged: the next roll's seed. */
	TMap<FString, int32> StagingRolls;
	/** Which file each undo (and redo) step belongs to, latest last. */
	TArray<FString> UndoFiles;
	TArray<FString> RedoFiles;

	UE::DreamNarrative::Graph::FDnaStoryGraph Graph;
	int32 ErrorCount = 0;
	FString Filter;
	TArray<FItemPtr> Roots;
	TSharedPtr<STreeView<FItemPtr>> Tree;
	TSharedPtr<class STextBlock> Status;
};
