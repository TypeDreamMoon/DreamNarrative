// Copyright DreamMoon. All Rights Reserved.

#include "SDnaStageTimeline.h"
#include "DreamNarrativeEditorSettings.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "Lang/DnaEditSession.h"
#include "Lang/DnaProject.h"
#include "Lang/DnaStageCompiler.h"
#include "LevelEditorViewport.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Rendering/DrawElements.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SDnaStageTimeline"

using namespace UE::DreamNarrative::Lang;

namespace
{
	constexpr int32 NumTracks = static_cast<int32>(EDnaStageTrack::Blocking) + 1;
	constexpr int32 NumRows = NumTracks + 1;  // the tracks and the restore block
	constexpr float RowHeight = 22.f;
	constexpr float RulerHeight = 18.f;
	constexpr float HeaderWidth = 76.f;
	constexpr float GrabWidth = 5.f;

	const TCHAR* RowName(int32 Row)
	{
		return Row < NumTracks ? DreamNarrative::LexToString(static_cast<EDnaStageTrack>(Row)) : TEXT("restore");
	}

	/** Muted, one hue per track. */
	FLinearColor TrackColor(EDnaStageTrack Track)
	{
		switch (Track)
		{
		case EDnaStageTrack::Camera: return FLinearColor(0.32f, 0.42f, 0.56f);
		case EDnaStageTrack::Light: return FLinearColor(0.56f, 0.50f, 0.32f);
		case EDnaStageTrack::Mask: return FLinearColor(0.46f, 0.38f, 0.52f);
		case EDnaStageTrack::Vfx: return FLinearColor(0.36f, 0.50f, 0.46f);
		case EDnaStageTrack::Ui: return FLinearColor(0.46f, 0.46f, 0.46f);
		case EDnaStageTrack::Anim: return FLinearColor(0.42f, 0.50f, 0.36f);
		case EDnaStageTrack::Game: return FLinearColor(0.54f, 0.40f, 0.36f);
		case EDnaStageTrack::Sequence: return FLinearColor(0.36f, 0.46f, 0.56f);
		case EDnaStageTrack::Audio: return FLinearColor(0.50f, 0.40f, 0.50f);
		case EDnaStageTrack::Blocking: return FLinearColor(0.44f, 0.48f, 0.40f);
		}
		return FLinearColor::Gray;
	}

	FString EntryLabel(const FDnaStageEntry& Entry)
	{
		FString Label = DreamNarrative::LexToString(Entry.Verb);
		if (!Entry.Target.IsNone()) { Label += TEXT(" ") + Entry.Target.ToString(); }
		else if (!Entry.Asset.IsEmpty()) { Label += TEXT(" ") + FPaths::GetBaseFilename(Entry.Asset); }
		else if (!Entry.Text.IsEmpty()) { Label += TEXT(" ") + Entry.Text.ToString(); }
		return Label;
	}
}

// ---------------------------------------------------------------- the track area

/** Draws the tracks and turns the mouse into timeline edits and scrubbing. */
class SDnaStageTrackArea : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SDnaStageTrackArea) {}
	SLATE_END_ARGS()

	void Construct(const FArguments&, SDnaStageTimeline* InOwner)
	{
		Owner = InOwner;
	}

	virtual FVector2D ComputeDesiredSize(float) const override
	{
		return FVector2D(600.f, RulerHeight + NumRows * RowHeight + 4.f);
	}

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling, FSlateWindowElementList& Out,
		int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override
	{
		const FSlateBrush* White = FCoreStyle::Get().GetBrush(TEXT("GenericWhiteBox"));
		const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", 8);
		const FVector2D Size = Geometry.GetLocalSize();
		auto Box = [&](float X, float Y, float W, float H, const FLinearColor& Color, int32 AtLayer)
		{
			FSlateDrawElement::MakeBox(Out, AtLayer, Geometry.ToPaintGeometry(FVector2D(FMath::Max(W, 1.f), FMath::Max(H, 1.f)), FSlateLayoutTransform(FVector2D(X, Y))), White, ESlateDrawEffect::None, Color);
		};
		auto Text = [&](float X, float Y, const FString& String, const FLinearColor& Color, int32 AtLayer)
		{
			FSlateDrawElement::MakeText(Out, AtLayer, Geometry.ToPaintGeometry(FVector2D(Size.X, RowHeight), FSlateLayoutTransform(FVector2D(X, Y))), String, Font, ESlateDrawEffect::None, Color);
		};

		Box(0.f, 0.f, Size.X, Size.Y, FLinearColor(0.02f, 0.02f, 0.02f), Layer);
		for (int32 Row = 0; Row < NumRows; ++Row)
		{
			const float Y = RulerHeight + Row * RowHeight;
			Box(0.f, Y, Size.X, RowHeight - 1.f, Row % 2 ? FLinearColor(0.035f, 0.035f, 0.035f) : FLinearColor(0.045f, 0.045f, 0.045f), Layer + 1);
			Text(6.f, Y + 4.f, RowName(Row), FLinearColor(0.6f, 0.6f, 0.6f), Layer + 2);
		}
		const FDnaStageData* Stage = Owner ? Owner->GetStage() : nullptr;
		if (!Stage)
		{
			Text(HeaderWidth, RulerHeight + 4.f, LOCTEXT("NoStage", "No stage, or it does not compile").ToString(), FLinearColor(0.7f, 0.5f, 0.4f), Layer + 3);
			return Layer + 3;
		}

		// -- the ruler: seconds, or tenths of the line
		const float End = TimeSpan(*Stage);
		const float Step = Stage->bLineRelative ? 0.1f : (End > 20.f ? 5.f : End > 6.f ? 1.f : 0.5f);
		for (float Tick = 0.f; Tick <= End + KINDA_SMALL_NUMBER; Tick += Step)
		{
			const float X = ToX(Geometry, *Stage, Tick);
			Box(X, 0.f, 1.f, Size.Y, FLinearColor(0.08f, 0.08f, 0.08f), Layer + 2);
			Text(X + 2.f, 2.f, Stage->bLineRelative ? FString::Printf(TEXT("%d%%"), FMath::RoundToInt(Tick * 100.f)) : FString::Printf(TEXT("%gs"), Tick), FLinearColor(0.5f, 0.5f, 0.5f), Layer + 3);
		}
		// The end of the stage.
		Box(ToX(Geometry, *Stage, Stage->Duration), 0.f, 2.f, Size.Y, FLinearColor(0.35f, 0.18f, 0.15f), Layer + 3);

		// -- the entries
		for (int32 Index = 0; Index < Stage->Entries.Num(); ++Index)
		{
			PaintEntry(Geometry, *Stage, Stage->Entries[Index], static_cast<int32>(Stage->Entries[Index].Track), Out, Layer + 4, Box, Text);
		}
		for (int32 Index = 0; Index < Stage->Restore.Num(); ++Index)
		{
			FDnaStageEntry AtEnd = Stage->Restore[Index];
			AtEnd.Start = AtEnd.End = Stage->Duration;
			PaintEntry(Geometry, *Stage, AtEnd, NumTracks, Out, Layer + 4, Box, Text);
		}

		// -- the playhead
		const float Head = ToX(Geometry, *Stage, Owner->GetPlayhead());
		Box(Head, 0.f, 2.f, Size.Y, FLinearColor(0.75f, 0.6f, 0.3f), Layer + 8);
		return Layer + 8;
	}

	virtual FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override
	{
		const FDnaStageData* Stage = Owner ? Owner->GetStage() : nullptr;
		if (!Stage || Event.GetEffectingButton() != EKeys::LeftMouseButton)
		{
			return FReply::Unhandled();
		}
		const FVector2D Local = Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition());
		Drag = FDrag();
		Drag.StartX = Local.X;
		if (HitTest(Geometry, *Stage, Local, Drag))
		{
			Drag.bActive = true;
		}
		else
		{
			bScrubbing = true;
			Owner->Scrub(FromX(Geometry, *Stage, Local.X));
		}
		return FReply::Handled().CaptureMouse(SharedThis(this));
	}

	virtual FReply OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event) override
	{
		const FDnaStageData* Stage = Owner ? Owner->GetStage() : nullptr;
		if (!Stage || !HasMouseCapture())
		{
			return FReply::Unhandled();
		}
		const FVector2D Local = Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition());
		if (bScrubbing)
		{
			Owner->Scrub(FromX(Geometry, *Stage, Local.X));
		}
		else if (Drag.bActive)
		{
			Drag.Delta = Snap(*Stage, (Local.X - Drag.StartX) / PixelsPerUnit(Geometry, *Stage));
			Invalidate(EInvalidateWidgetReason::Paint);
		}
		return FReply::Handled();
	}

	virtual FReply OnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event) override
	{
		if (!HasMouseCapture())
		{
			return FReply::Unhandled();
		}
		const FDnaStageData* Stage = Owner ? Owner->GetStage() : nullptr;
		if (Stage && Drag.bActive && Drag.Delta != 0.f)
		{
			// Only now does the text change: a drag is one edit, one undo.
			FText Error;
			const FDnaStageEntry& Entry = Drag.Entry;
			const int32 Line = Entry.SourceLine;
			switch (Drag.Part)
			{
			case EPart::Body:
				Owner->MoveEntry(Line, FMath::Max(Entry.Start + Drag.Delta, 0.f), Entry.End + Drag.Delta, Error);
				break;
			case EPart::End:
				Owner->MoveEntry(Line, Entry.Start, FMath::Max(Entry.End + Drag.Delta, Entry.Start), Error);
				break;
			case EPart::Span:
				Owner->SetEntrySpan(Line, FMath::Max(Entry.Over + Drag.Delta, 0.f), Error);
				break;
			}
		}
		Drag = FDrag();
		bScrubbing = false;
		Invalidate(EInvalidateWidgetReason::Paint);
		return FReply::Handled().ReleaseMouseCapture();
	}

	virtual FReply OnMouseButtonDoubleClick(const FGeometry& Geometry, const FPointerEvent& Event) override
	{
		const FDnaStageData* Stage = Owner ? Owner->GetStage() : nullptr;
		FDrag Hit;
		if (Stage && HitTest(Geometry, *Stage, Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition()), Hit))
		{
			Owner->OpenLine(Hit.Entry.SourceLine);
			return FReply::Handled();
		}
		return FReply::Unhandled();
	}

private:
	enum class EPart : uint8 { Body, End, Span };

	struct FDrag
	{
		bool bActive = false;
		FDnaStageEntry Entry;
		EPart Part = EPart::Body;
		float StartX = 0.f;
		float Delta = 0.f;
	};

	/** The time the track area shows: the stage, its tail and a little room. */
	static float TimeSpan(const FDnaStageData& Stage)
	{
		return FMath::Max((Stage.Duration + Stage.GetTail()) * 1.1f, Stage.bLineRelative ? 1.f : 0.5f);
	}

	static float PixelsPerUnit(const FGeometry& Geometry, const FDnaStageData& Stage)
	{
		return FMath::Max(Geometry.GetLocalSize().X - HeaderWidth - 8.f, 50.f) / TimeSpan(Stage);
	}

	static float ToX(const FGeometry& Geometry, const FDnaStageData& Stage, float Time)
	{
		return HeaderWidth + Time * PixelsPerUnit(Geometry, Stage);
	}

	static float FromX(const FGeometry& Geometry, const FDnaStageData& Stage, float X)
	{
		return FMath::Max((X - HeaderWidth) / PixelsPerUnit(Geometry, Stage), 0.f);
	}

	/** Twentieths of a second, or hundredths of the line. */
	static float Snap(const FDnaStageData& Stage, float Value)
	{
		const float Grain = Stage.bLineRelative ? 0.01f : 0.05f;
		return FMath::RoundToFloat(Value / Grain) * Grain;
	}

	/** What is under a point: an entry's body, a range's end or a span's tail. */
	bool HitTest(const FGeometry& Geometry, const FDnaStageData& Stage, const FVector2D& Local, FDrag& Out) const
	{
		const int32 Row = FMath::FloorToInt((Local.Y - RulerHeight) / RowHeight);
		if (Row < 0 || Row >= NumTracks)
		{
			return false;
		}
		for (int32 Index = Stage.Entries.Num() - 1; Index >= 0; --Index)
		{
			const FDnaStageEntry& Entry = Stage.Entries[Index];
			if (static_cast<int32>(Entry.Track) != Row)
			{
				continue;
			}
			const float X0 = ToX(Geometry, Stage, Entry.Start);
			const float X1 = ToX(Geometry, Stage, Entry.End);
			const float Tail = ToX(Geometry, Stage, Entry.Start + Entry.Over);
			Out.Entry = Entry;
			if (Entry.bRange && FMath::Abs(Local.X - X1) <= GrabWidth) { Out.Part = EPart::End; return true; }
			if (Entry.Over > 0.f && FMath::Abs(Local.X - Tail) <= GrabWidth) { Out.Part = EPart::Span; return true; }
			if ((Entry.bRange && Local.X >= X0 - GrabWidth && Local.X <= X1) || FMath::Abs(Local.X - X0) <= GrabWidth + 1.f) { Out.Part = EPart::Body; return true; }
		}
		return false;
	}

	template <typename TBoxFunction, typename TTextFunction>
	void PaintEntry(const FGeometry& Geometry, const FDnaStageData& Stage, const FDnaStageEntry& Original, int32 Row, FSlateWindowElementList& Out,
		int32 Layer, TBoxFunction& Box, TTextFunction& Text) const
	{
		FDnaStageEntry Entry = Original;
		// The entry being dragged is drawn where it would land.
		if (Drag.bActive && Row < NumTracks && Drag.Entry.SourceLine == Entry.SourceLine)
		{
			switch (Drag.Part)
			{
			case EPart::Body: Entry.Start += Drag.Delta; Entry.End += Drag.Delta; break;
			case EPart::End: Entry.End = FMath::Max(Entry.End + Drag.Delta, Entry.Start); break;
			case EPart::Span: Entry.Over = FMath::Max(Entry.Over + Drag.Delta, 0.f); break;
			}
		}
		const FLinearColor Color = TrackColor(Original.Track);
		const float Y = RulerHeight + Row * RowHeight + 3.f;
		const float X0 = ToX(Geometry, Stage, Entry.Start);
		if (Entry.Over > 0.f)
		{
			Box(X0, Y + RowHeight * 0.5f - 3.f, Entry.Over * PixelsPerUnit(Geometry, Stage), 4.f, Color * 0.6f, Layer);
		}
		if (Entry.bRange)
		{
			Box(X0, Y, FMath::Max(ToX(Geometry, Stage, Entry.End) - X0, 2.f), RowHeight - 7.f, Color, Layer + 1);
		}
		else
		{
			Box(X0 - 3.f, Y + 2.f, 6.f, RowHeight - 11.f, Color * 1.4f, Layer + 1);
		}
		Text(X0 + 5.f, Y, EntryLabel(Entry), FLinearColor(0.85f, 0.85f, 0.85f), Layer + 2);
	}

	SDnaStageTimeline* Owner = nullptr;
	FDrag Drag;
	bool bScrubbing = false;
};

// ---------------------------------------------------------------- the timeline

void SDnaStageTimeline::Construct(const FArguments& InArgs)
{
	LoadFiles(InArgs._Files);

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(4.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
			[
				SNew(SComboBox<TSharedPtr<FStageOption>>)
				.OptionsSource(&Options)
				.OnGenerateWidget_Lambda([](TSharedPtr<FStageOption> Option) { return SNew(STextBlock).Text(FText::FromString(Option->Label)); })
				.OnSelectionChanged_Lambda([this](TSharedPtr<FStageOption> Option, ESelectInfo::Type) { if (Option) { SelectStage(Option->Name); } })
				[
					SNew(STextBlock).Text_Lambda([this]() { return Selected ? FText::FromString(Selected->Label) : LOCTEXT("PickStage", "Pick a stage"); })
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
			[
				SNew(SCheckBox)
				.ToolTipText(LOCTEXT("LineTip", "Follow the line it is attached to (`base: line`) instead of counting in seconds"))
				.IsChecked_Lambda([this]() { return GetStage() && GetStage()->bLineRelative ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState State)
				{
					FText Error;
					SetTimeBase(State == ECheckBoxState::Checked, PreviewLineSeconds, Error);
				})
				[
					SNew(STextBlock).Text(LOCTEXT("FollowLine", "Follows its line"))
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
			[
				SNew(SBox).WidthOverride(90.f)
				[
					SNew(SSpinBox<float>)
					.ToolTipText(LOCTEXT("LineSecondsTip", "Seconds of line a line-following stage is previewed and converted against"))
					.MinValue(0.1f).MaxValue(60.f).Delta(0.1f)
					.Value_Lambda([this]() { return PreviewLineSeconds; })
					.OnValueChanged_Lambda([this](float Value) { PreviewLineSeconds = Value; Scrub(Playhead); })
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
			[
				SNew(SCheckBox)
				.ToolTipText(LOCTEXT("ViewportTip", "Fly the level viewport through the stage's shots, and walk the level's actors through its blocking, as the playhead moves"))
				.IsChecked_Lambda([this]() { return bPreviewInViewport ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState State) { SetPreviewInViewport(State == ECheckBoxState::Checked); })
				[
					SNew(STextBlock).Text(LOCTEXT("Viewport", "Preview in viewport"))
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2.f, 0.f)
			[
				SNew(SButton).Text(LOCTEXT("Undo", "Undo")).IsEnabled_Lambda([this]() { const auto Session = SelectedSession(); return Session && Session->CanUndo(); }).OnClicked_Lambda([this]() { Undo(); return FReply::Handled(); })
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2.f, 0.f)
			[
				SNew(SButton).Text(LOCTEXT("Redo", "Redo")).IsEnabled_Lambda([this]() { const auto Session = SelectedSession(); return Session && Session->CanRedo(); }).OnClicked_Lambda([this]() { Redo(); return FReply::Handled(); })
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2.f, 0.f)
			[
				SNew(SButton).Text(LOCTEXT("Save", "Save")).IsEnabled_Lambda([this]() { return HasUnsavedEdits(); }).OnClicked_Lambda([this]() { if (!SaveAll()) { ResolveConflicts(); } return FReply::Handled(); })
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(4.f, 0.f)
		[
			SAssignNew(Area, SDnaStageTrackArea, this)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(6.f, 4.f)
		[
			SAssignNew(PoseText, STextBlock).AutoWrapText(true)
		]
		+ SVerticalBox::Slot().FillHeight(1.f).Padding(6.f, 2.f)
		[
			SNew(STextBlock).AutoWrapText(true).ColorAndOpacity(FLinearColor(0.75f, 0.55f, 0.45f)).Text_Lambda([this]() { return GetStatusText(); })
		]
	];

	if (Options.Num() > 0)
	{
		SelectStage(Options[0]->Name);
	}
}

SDnaStageTimeline::~SDnaStageTimeline()
{
	SetPreviewInViewport(false);
}

void SDnaStageTimeline::LoadFiles(const TArray<FString>& InFiles)
{
	Files = InFiles;
	if (Files.Num() == 0)
	{
		FindDnaSources(GetDnaSourceRoot(), Files);
	}
	for (const FString& File : Files)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *File))
		{
			continue;
		}
		TSharedPtr<FDnaEditSession> Session = MakeShared<FDnaEditSession>();
		Session->Open(File, Text);
		Sessions.Add(File, Session);
	}
	RefreshOptions();
}

void SDnaStageTimeline::RefreshOptions()
{
	const FName Was = GetSelectedStage();
	Options.Reset();
	Selected.Reset();
	for (const TPair<FString, TSharedPtr<FDnaEditSession>>& Pair : Sessions)
	{
		for (const FDnaLineNodePtr& Root : Pair.Value->GetDocument().Roots)
		{
			if (Root.IsValid() && Root->Kind == EDnaLineKind::Stage && !Root->Payload.Name.IsEmpty())
			{
				TSharedPtr<FStageOption> Option = MakeShared<FStageOption>();
				Option->File = Pair.Key;
				Option->Name = FName(*Root->Payload.Name);
				Option->Label = FPaths::GetBaseFilename(Pair.Key) + TEXT(" · ") + Root->Payload.Name;
				Options.Add(Option);
				if (Option->Name == Was) { Selected = Option; }
			}
		}
	}
}

TArray<FName> SDnaStageTimeline::GetStageNames() const
{
	TArray<FName> Out;
	for (const TSharedPtr<FStageOption>& Option : Options) { Out.Add(Option->Name); }
	return Out;
}

bool SDnaStageTimeline::SelectStage(FName Stage)
{
	for (const TSharedPtr<FStageOption>& Option : Options)
	{
		if (Option->Name == Stage)
		{
			// Another stage's blocking starts from where everyone stood.
			PutBackPreviewActors();
			Selected = Option;
			Playhead = 0.f;
			Recompile();
			return true;
		}
	}
	return false;
}

FName SDnaStageTimeline::GetSelectedStage() const
{
	return Selected ? Selected->Name : NAME_None;
}

TSharedPtr<FDnaEditSession> SDnaStageTimeline::SelectedSession() const
{
	const TSharedPtr<FDnaEditSession>* Session = Selected ? Sessions.Find(Selected->File) : nullptr;
	return Session ? *Session : nullptr;
}

void SDnaStageTimeline::Recompile()
{
	bCompiled = false;
	Diagnostics.Reset();
	const TSharedPtr<FDnaEditSession> Session = SelectedSession();
	if (!Session)
	{
		return;
	}
	for (const FDnaLineNodePtr& Root : Session->GetDocument().Roots)
	{
		if (Root.IsValid() && Root->Kind == EDnaLineKind::Stage && FName(*Root->Payload.Name) == Selected->Name)
		{
			FDnaDiagnosticSink Sink;
			Sink.SetFilePath(Session->GetPath());
			CompileDnaStage(*Root, Compiled, Sink);
			bCompiled = true;
			for (const FDnaDiagnostic& Diagnostic : Sink.GetDiagnostics())
			{
				Diagnostics.Add(FDnaDiagnosticSink::ToLogString(Diagnostic));
			}
			break;
		}
	}
	for (const FString& Error : Session->GetParseErrors())
	{
		Diagnostics.Add(Error);
	}
	Scrub(Playhead);
}

FString SDnaStageTimeline::GetText() const
{
	const TSharedPtr<FDnaEditSession> Session = SelectedSession();
	return Session ? Session->GetText() : FString();
}

bool SDnaStageTimeline::HasUnsavedEdits() const
{
	for (const TPair<FString, TSharedPtr<FDnaEditSession>>& Pair : Sessions)
	{
		if (Pair.Value->IsDirty()) { return true; }
	}
	return false;
}

// ---------------------------------------------------------------- edits

bool SDnaStageTimeline::MoveEntry(int32 Line, float Start, float End, FText& OutError)
{
	const TSharedPtr<FDnaEditSession> Session = SelectedSession();
	const bool bOk = Session && Session->SetStageEntryTime(Line, Start, End, OutError);
	Recompile();
	return bOk;
}

bool SDnaStageTimeline::SetEntrySpan(int32 Line, float Over, FText& OutError)
{
	const TSharedPtr<FDnaEditSession> Session = SelectedSession();
	const bool bOk = Session && Session->SetStageEntrySpan(Line, Over, OutError);
	Recompile();
	return bOk;
}

bool SDnaStageTimeline::AddEntry(EDnaStageTrack Track, float Start, const FString& VerbAndArgs, FText& OutError)
{
	const TSharedPtr<FDnaEditSession> Session = SelectedSession();
	const bool bOk = Session && Session->AddStageEntry(Selected->Name.ToString(), DreamNarrative::LexToString(Track), Start, Start, false, VerbAndArgs, OutError);
	Recompile();
	return bOk;
}

bool SDnaStageTimeline::RemoveEntry(int32 Line, FText& OutError)
{
	const TSharedPtr<FDnaEditSession> Session = SelectedSession();
	const bool bOk = Session && Session->RemoveStageEntry(Line, OutError);
	Recompile();
	return bOk;
}

bool SDnaStageTimeline::SetTimeBase(bool bLine, float LineSeconds, FText& OutError)
{
	const TSharedPtr<FDnaEditSession> Session = SelectedSession();
	const bool bOk = Session && Session->SetStageTimeBase(Selected->Name.ToString(), bLine, LineSeconds, OutError);
	Playhead = 0.f;
	Recompile();
	return bOk;
}

bool SDnaStageTimeline::Undo()
{
	const TSharedPtr<FDnaEditSession> Session = SelectedSession();
	const bool bOk = Session && Session->Undo();
	Recompile();
	return bOk;
}

bool SDnaStageTimeline::Redo()
{
	const TSharedPtr<FDnaEditSession> Session = SelectedSession();
	const bool bOk = Session && Session->Redo();
	Recompile();
	return bOk;
}

bool SDnaStageTimeline::SaveAll()
{
	// All of them or none: no file is written while another one meets a change on disk.
	bool bOk = true;
	for (const TPair<FString, TSharedPtr<FDnaEditSession>>& Pair : Sessions)
	{
		FText Error;
		if (Pair.Value->IsDirty() && !Pair.Value->CanSave(Error))
		{
			Diagnostics.Add(Error.ToString());
			bOk = false;
		}
	}
	if (!bOk)
	{
		return false;
	}
	for (const TPair<FString, TSharedPtr<FDnaEditSession>>& Pair : Sessions)
	{
		FDnaEditSession& Session = *Pair.Value;
		if (!Session.IsDirty())
		{
			continue;
		}
		FText Error;
		if (!Session.Save(Error))
		{
			Diagnostics.Add(Error.ToString());
			bOk = false;
		}
	}
	return bOk;
}

TArray<FString> SDnaStageTimeline::GetConflictedFiles() const
{
	TArray<FString> Out;
	for (const TPair<FString, TSharedPtr<FDnaEditSession>>& Pair : Sessions)
	{
		if (Pair.Value->IsDirty() && Pair.Value->IsChangedOnDisk())
		{
			Out.Add(Pair.Key);
		}
	}
	return Out;
}

bool SDnaStageTimeline::DiscardEdits(const FString& File, FText& OutError)
{
	const TSharedPtr<FDnaEditSession>* Session = Sessions.Find(File);
	if (!Session || !(*Session)->Revert(OutError))
	{
		return false;
	}
	RefreshOptions();
	Recompile();
	return true;
}

bool SDnaStageTimeline::OverwriteDisk(const FString& File, FText& OutError)
{
	const TSharedPtr<FDnaEditSession>* Session = Sessions.Find(File);
	return Session && (*Session)->Save(OutError, /*bOverwriteExternal=*/true);
}

void SDnaStageTimeline::ResolveConflicts()
{
	const TArray<FString> Conflicted = GetConflictedFiles();
	if (Conflicted.Num() == 0)
	{
		return;
	}
	const UE::DreamNarrative::Editor::EDnaConflictChoice Choice = UE::DreamNarrative::Editor::AskAboutConflicts(Conflicted);
	if (Choice == UE::DreamNarrative::Editor::EDnaConflictChoice::Keep)
	{
		return;
	}
	Diagnostics.Reset();
	for (const FString& File : Conflicted)
	{
		FText Error;
		const bool bOk = Choice == UE::DreamNarrative::Editor::EDnaConflictChoice::Discard ? DiscardEdits(File, Error) : OverwriteDisk(File, Error);
		if (!bOk) { Diagnostics.Add(Error.ToString()); }
	}
}

void SDnaStageTimeline::OpenLine(int32 Line) const
{
	if (Selected)
	{
		UE::DreamNarrative::Editor::OpenSource(Selected->File, Line);
	}
}

// ---------------------------------------------------------------- the playhead

void SDnaStageTimeline::Scrub(float Time)
{
	Playhead = FMath::Max(Time, 0.f);
	const FDnaStageData* Stage = GetStage();
	Pose = Stage ? DreamNarrative::SampleDnaStage(*Stage, Playhead, Stage->bLineRelative ? PreviewLineSeconds : 1.f) : FDnaStagePose();
	if (PoseText)
	{
		const FString When = Stage && Stage->bLineRelative ? FString::Printf(TEXT("%.0f%%"), Playhead * 100.f) : FString::Printf(TEXT("%.2fs"), Playhead);
		PoseText->SetText(FText::FromString(When + TEXT("  ") + Pose.Describe()));
	}
	ApplyViewportPreview();
	if (Area)
	{
		Area->Invalidate(EInvalidateWidgetReason::Paint);
	}
}

FText SDnaStageTimeline::GetStatusText() const
{
	return Diagnostics.Num() > 0 ? FText::FromString(FString::Join(Diagnostics, TEXT("\n"))) : FText::GetEmpty();
}

void SDnaStageTimeline::SetPreviewInViewport(bool bOn)
{
	FLevelEditorViewportClient* Viewport = GIsEditor ? GCurrentLevelEditingViewportClient : nullptr;
	if (bOn && !bPreviewInViewport && Viewport)
	{
		ViewportStartLocation = Viewport->GetViewLocation();
		ViewportStartRotation = Viewport->GetViewRotation();
		ViewportStartFov = Viewport->ViewFOV;
		bHaveViewportStart = true;
	}
	else if (!bOn && bPreviewInViewport && Viewport && bHaveViewportStart)
	{
		Viewport->SetViewLocation(ViewportStartLocation);
		Viewport->SetViewRotation(ViewportStartRotation);
		Viewport->ViewFOV = ViewportStartFov;
		Viewport->Invalidate();
	}
	if (!bOn)
	{
		PutBackPreviewActors();
	}
	// The actors walk with no viewport too; the camera needs one.
	bPreviewInViewport = bOn;
	ApplyViewportPreview();
}

void SDnaStageTimeline::ApplyViewportPreview()
{
	const FDnaStageData* Stage = GetStage();
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!bPreviewInViewport || !Stage || !World)
	{
		return;
	}
	// The editor world's actors stand in for the stage's roles by their tags, as in the game.
	auto Resolve = [World](FName Role) -> AActor*
	{
		if (Role.IsNone()) { return nullptr; }
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->ActorHasTag(Role)) { return *It; }
		}
		return nullptr;
	};

	// The blocking places them as the director would, each from where it stood before the preview.
	const TMap<AActor*, FTransform> Places = DreamNarrative::SampleDnaStageBlocking(*Stage, Playhead, Resolve, [this](const AActor* Actor) -> FTransform
	{
		const TWeakObjectPtr<AActor> Key(const_cast<AActor*>(Actor));
		if (const FTransform* Known = PreviewOrigins.Find(Key)) { return *Known; }
		return PreviewOrigins.Add(Key, Actor->GetActorTransform());
	});
	for (const TPair<TWeakObjectPtr<AActor>, FTransform>& Origin : PreviewOrigins)
	{
		if (AActor* Actor = Origin.Key.Get())
		{
			const FTransform* Place = Places.Find(Actor);
			Actor->SetActorTransform(Place ? *Place : Origin.Value);
		}
	}

	FLevelEditorViewportClient* Viewport = GIsEditor ? GCurrentLevelEditingViewportClient : nullptr;
	if (!Viewport || !bHaveViewportStart)
	{
		return;
	}
	FMinimalViewInfo Start;
	Start.Location = ViewportStartLocation;
	Start.Rotation = ViewportStartRotation;
	Start.FOV = ViewportStartFov;
	FMinimalViewInfo View = Start;
	if (Pose.Camera.bActive)
	{
		View = DreamNarrative::EvaluateDnaStageView(*Stage, Pose.Camera, Start, Resolve);
	}
	Viewport->SetViewLocation(View.Location);
	Viewport->SetViewRotation(View.Rotation);
	Viewport->ViewFOV = View.FOV;
	Viewport->Invalidate();
}

void SDnaStageTimeline::PutBackPreviewActors()
{
	for (const TPair<TWeakObjectPtr<AActor>, FTransform>& Origin : PreviewOrigins)
	{
		if (AActor* Actor = Origin.Key.Get())
		{
			Actor->SetActorTransform(Origin.Value);
		}
	}
	PreviewOrigins.Reset();
}

#undef LOCTEXT_NAMESPACE
