// Copyright DreamMoon. All Rights Reserved.
//
// The stage timeline: one stage of the project's sources at a time, its tracks as rows and its entries
// on them. Dragging an entry, the end of a range or the tail of a blend writes that one line of the
// source through an edit session; the time base can be switched (seconds, or following a line);
// the playhead shows the pose of the stage at that moment -- the same function the director plays
// with -- and can fly the level viewport's camera through the stage's shots.

#pragma once

#include "CoreMinimal.h"
#include "DreamNarrativeStageData.h"
#include "DreamNarrativeStagePose.h"
#include "Widgets/SCompoundWidget.h"

namespace UE::DreamNarrative::Lang { class FDnaEditSession; }

class STextBlock;
class SDnaStageTrackArea;

class DREAMNARRATIVEEDITOR_API SDnaStageTimeline : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDnaStageTimeline) {}
		/** The files to read; empty for every `.dna` under the project's source folder. */
		SLATE_ARGUMENT(TArray<FString>, Files)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SDnaStageTimeline() override;

	// ---------------------------------------------------------------- what the mouse does, by hand

	/** Every stage the files declare, `文件 · 演出` labels aside. */
	TArray<FName> GetStageNames() const;
	bool SelectStage(FName Stage);
	FName GetSelectedStage() const;

	/** The selected stage as its file's text compiles now; null when none is selected or it does not compile. */
	const FDnaStageData* GetStage() const { return bCompiled ? &Compiled : nullptr; }

	/** The checks the stage fails as it stands, `file(line,column): DNSxxxx message` each. */
	const TArray<FString>& GetDiagnostics() const { return Diagnostics; }

	/** Moves the entry on a line of the stage's file: Start, and End for a range, in the stage's unit. */
	bool MoveEntry(int32 Line, float Start, float End, FText& OutError);

	/** Sets how long the entry on a line takes to blend, fade, ramp or push. */
	bool SetEntrySpan(int32 Line, float Over, FText& OutError);

	bool AddEntry(EDnaStageTrack Track, float Start, const FString& VerbAndArgs, FText& OutError);
	bool RemoveEntry(int32 Line, FText& OutError);

	/** Seconds, or following its line; LineSeconds is the line the times are converted against. */
	bool SetTimeBase(bool bLine, float LineSeconds, FText& OutError);

	/** Puts the playhead at Time (the stage's unit); the preview pose follows. */
	void Scrub(float Time);
	float GetPlayhead() const { return Playhead; }
	const FDnaStagePose& GetPreviewPose() const { return Pose; }

	/** How long a line a line-relative stage is previewed against, in seconds. */
	float GetPreviewLineSeconds() const { return PreviewLineSeconds; }

	/** The text of the selected stage's file, edits included. */
	FString GetText() const;
	bool HasUnsavedEdits() const;
	bool Undo();
	bool Redo();
	bool SaveAll();
	/** Files with unsaved edits whose text on disk changed since the timeline read or saved them. */
	TArray<FString> GetConflictedFiles() const;
	/** Drops a file's unsaved edits and undo history and reads it from disk again. */
	bool DiscardEdits(const FString& File, FText& OutError);
	/** Writes a file's edits over the change made on disk. */
	bool OverwriteDisk(const FString& File, FText& OutError);

	/**
	 * Flies the active level viewport through the stage's camera while scrubbing, and walks the level's actors
	 * through its blocking; off puts the viewport and the actors back.
	 */
	void SetPreviewInViewport(bool bOn);

	/** Opens the source of the entry on a line in the configured editor. */
	void OpenLine(int32 Line) const;

private:
	struct FStageOption
	{
		FString File;
		FName Name;
		FString Label;
	};

	void LoadFiles(const TArray<FString>& InFiles);
	void RefreshOptions();
	TSharedPtr<UE::DreamNarrative::Lang::FDnaEditSession> SelectedSession() const;
	/** Compiles the selected stage from its session's text, after every edit. */
	void Recompile();
	void ApplyViewportPreview();
	/** Puts the actors the preview moved back where they stood. */
	void PutBackPreviewActors();
	FText GetStatusText() const;
	/** After a refused save: asks what to do with the files changed on disk, and does it. */
	void ResolveConflicts();

	TArray<FString> Files;
	TMap<FString, TSharedPtr<UE::DreamNarrative::Lang::FDnaEditSession>> Sessions;
	TArray<TSharedPtr<FStageOption>> Options;
	TSharedPtr<FStageOption> Selected;

	FDnaStageData Compiled;
	bool bCompiled = false;
	TArray<FString> Diagnostics;

	float Playhead = 0.f;
	FDnaStagePose Pose;
	float PreviewLineSeconds = 3.f;

	bool bPreviewInViewport = false;
	bool bHaveViewportStart = false;
	FVector ViewportStartLocation = FVector::ZeroVector;
	FRotator ViewportStartRotation = FRotator::ZeroRotator;
	float ViewportStartFov = 90.f;
	/** Where the actors the preview has placed stood before it. */
	TMap<TWeakObjectPtr<AActor>, FTransform> PreviewOrigins;

	TSharedPtr<SDnaStageTrackArea> Area;
	TSharedPtr<STextBlock> PoseText;

	friend class SDnaStageTrackArea;
};
