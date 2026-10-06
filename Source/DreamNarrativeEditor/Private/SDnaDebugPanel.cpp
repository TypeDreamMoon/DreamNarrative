// Copyright DreamMoon. All Rights Reserved.

#include "SDnaDebugPanel.h"
#include "DreamNarrativeDialogueRunner.h"
#include "DreamNarrativeLedger.h"
#include "DreamNarrativeStory.h"
#include "Editor.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SDnaDebugPanel"

namespace
{
	FString DescribeLine(const FDnaLineView& Line)
	{
		return Line.bNarration ? Line.Text.ToString() : FString::Printf(TEXT("%s: %s"), *Line.Speaker.ToString(), *Line.Text.ToString());
	}

	FString Scope(const FDnaFactDecl& Decl)
	{
		TArray<FString> Parts;
		if (Decl.Owner == EDnaFactOwner::Extern) { Parts.Add(TEXT("extern")); }
		if (Decl.bSession) { Parts.Add(TEXT("session")); }
		if (Decl.bProfile) { Parts.Add(TEXT("profile")); }
		if (Decl.bProgress) { Parts.Add(TEXT("progress")); }
		return Parts.Num() > 0 ? TEXT("  [") + FString::Join(Parts, TEXT(" ")) + TEXT("]") : FString();
	}
}

void SDnaDebugPanel::Construct(const FArguments& InArgs)
{
	FixedStory = InArgs._Story;

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(4.f)
		[
			SAssignNew(Header, STextBlock)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(2.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(2.f)
			[
				SAssignNew(ChapterBox, SEditableTextBox).HintText(LOCTEXT("ChapterHint", "chapter"))
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2.f)
			[
				SNew(SButton).Text(LOCTEXT("Jump", "Jump")).ToolTipText(LOCTEXT("JumpTip", "The debug teleport: implied facts along the way, no checkpoint"))
				.OnClicked_Lambda([this]()
				{
					if (UDreamNarrativeStory* Story = FindStory()) { Story->GetLedger()->JumpToChapter(FName(*ChapterBox->GetText().ToString())); }
					Refresh();
					return FReply::Handled();
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2.f)
			[
				SNew(SButton).Text(LOCTEXT("Select", "Select")).ToolTipText(LOCTEXT("SelectTip", "Back to the chapter's checkpoint, progress kept"))
				.OnClicked_Lambda([this]()
				{
					if (UDreamNarrativeStory* Story = FindStory()) { Story->GetLedger()->SelectChapter(FName(*ChapterBox->GetText().ToString())); }
					Refresh();
					return FReply::Handled();
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2.f)
			[
				SNew(SButton).Text(LOCTEXT("NewGame", "New playthrough"))
				.OnClicked_Lambda([this]()
				{
					if (UDreamNarrativeStory* Story = FindStory()) { Story->GetLedger()->StartNewPlaythrough(); }
					Refresh();
					return FReply::Handled();
				})
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(4.f)
		[
			SAssignNew(ChannelBox, SVerticalBox)
		]
		+ SVerticalBox::Slot().FillHeight(1.f)
		[
			SNew(SSplitter).Orientation(Orient_Vertical)
			+ SSplitter::Slot().Value(0.65f)
			[
				SAssignNew(FactList, SListView<TSharedPtr<FString>>)
				.ListItemsSource(&FactRows)
				.OnGenerateRow(this, &SDnaDebugPanel::MakeTextRow)
			]
			+ SSplitter::Slot().Value(0.35f)
			[
				SAssignNew(WriteList, SListView<TSharedPtr<FString>>)
				.ListItemsSource(&Writes)
				.OnGenerateRow(this, &SDnaDebugPanel::MakeTextRow)
			]
		]
	];

	RegisterActiveTimer(0.25f, FWidgetActiveTimerDelegate::CreateLambda([this](double, float)
	{
		Refresh();
		return EActiveTimerReturnType::Continue;
	}));
	Refresh();
}

SDnaDebugPanel::~SDnaDebugPanel()
{
	Unbind();
}

UDreamNarrativeStory* SDnaDebugPanel::FindStory() const
{
	if (FixedStory.IsValid())
	{
		return FixedStory.Get();
	}
	if (!GEngine)
	{
		return nullptr;
	}
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		UWorld* World = Context.World();
		if ((Context.WorldType == EWorldType::PIE || Context.WorldType == EWorldType::Game) && World && World->GetGameInstance())
		{
			if (UDreamNarrativeStory* Story = World->GetGameInstance()->GetSubsystem<UDreamNarrativeStory>())
			{
				return Story;
			}
		}
	}
	return nullptr;
}

void SDnaDebugPanel::Bind(UDreamNarrativeStory* Story)
{
	if (BoundStory.Get() == Story)
	{
		return;
	}
	Unbind();
	Writes.Reset();
	BoundStory = Story;
	if (Story && Story->GetLedger())
	{
		WriteHandle = Story->GetLedger()->OnFactWritten.AddLambda([this](FName Fact, const FDnaValue& Value, EDnaWriteSource Source)
		{
			Writes.Insert(MakeShared<FString>(FString::Printf(TEXT("%s = %s    by %s"), *Fact.ToString(), *Value.ToString(),
				Source == EDnaWriteSource::Game ? TEXT("the game") : TEXT("the story"))), 0);
			if (Writes.Num() > 200) { Writes.SetNum(200); }
			if (WriteList.IsValid()) { WriteList->RequestListRefresh(); }
		});
	}
}

void SDnaDebugPanel::Unbind()
{
	if (UDreamNarrativeStory* Story = BoundStory.Get())
	{
		if (Story->GetLedger())
		{
			Story->GetLedger()->OnFactWritten.Remove(WriteHandle);
		}
	}
	WriteHandle.Reset();
	BoundStory.Reset();
}

void SDnaDebugPanel::Refresh()
{
	UDreamNarrativeStory* Story = FindStory();
	Bind(Story);
	UDreamNarrativeLedger* Ledger = Story ? Story->GetLedger() : nullptr;
	if (!Ledger)
	{
		Header->SetText(LOCTEXT("NoGame", "No story is running. Start Play In Editor."));
		if (FactRows.Num() > 0) { FactRows.Reset(); FactList->RequestListRefresh(); }
		if (!ChannelsShown.IsEmpty()) { ChannelsShown.Reset(); ChannelBox->ClearChildren(); }
		return;
	}

	TArray<FString> Checkpoints;
	for (const FName& Chapter : Ledger->GetChapterCheckpoints()) { Checkpoints.Add(Chapter.ToString()); }
	Header->SetText(FText::Format(LOCTEXT("Header", "chapter {0}    checkpoints: {1}    input {2}"),
		FText::FromName(Ledger->GetChapter()), FText::FromString(Checkpoints.Num() > 0 ? FString::Join(Checkpoints, TEXT(", ")) : TEXT("none")),
		Story->IsInputLocked() ? LOCTEXT("Locked", "locked") : LOCTEXT("Free", "free")));

	// Facts: rebuilt only when something reads differently, so a selection survives.
	TArray<FName> Names;
	Ledger->GetFactNames(Names);
	Names.Sort(FNameLexicalLess());
	TArray<FString> Rows;
	for (const FName& Name : Names)
	{
		const FDnaFactDecl* Decl = Ledger->FindFactDecl(Name);
		Rows.Add(FString::Printf(TEXT("%s = %s%s"), *Name.ToString(), *Ledger->GetFact(Name).ToString(), Decl ? *Scope(*Decl) : TEXT("")));
	}
	bool bSame = Rows.Num() == FactRows.Num();
	for (int32 Index = 0; bSame && Index < Rows.Num(); ++Index) { bSame = *FactRows[Index] == Rows[Index]; }
	if (!bSame)
	{
		FactRows.Reset();
		for (FString& Row : Rows) { FactRows.Add(MakeShared<FString>(MoveTemp(Row))); }
		FactList->RequestListRefresh();
	}
	RebuildChannels(*Story);
}

void SDnaDebugPanel::RebuildChannels(UDreamNarrativeStory& Story)
{
	TArray<FName> Channels;
	Story.GetChannelNames(Channels);
	FString Shown;
	for (const FName& Channel : Channels)
	{
		const UDreamNarrativeDialogueRunner* Runner = Story.GetRunner(Channel);
		Shown += FString::Printf(TEXT("%s|%p|%d|%d|%d;"), *Channel.ToString(), Runner, Runner ? Runner->GetLinesShown() : 0,
			Runner ? static_cast<int32>(Runner->GetState()) : -1, Runner && Runner->IsPaused() ? 1 : 0);
	}
	if (Shown == ChannelsShown)
	{
		return;
	}
	ChannelsShown = Shown;
	ChannelBox->ClearChildren();
	TWeakObjectPtr<UDreamNarrativeStory> WeakStory(&Story);
	for (const FName& Channel : Channels)
	{
		UDreamNarrativeDialogueRunner* Runner = Story.GetRunner(Channel);
		const int32 Waiting = Story.GetQueueLength(Channel);
		FString Text = FString::Printf(TEXT("[%s] "), *Channel.ToString());
		if (!Runner)
		{
			Text += Waiting > 0 ? FString::Printf(TEXT("quiet, %d waiting"), Waiting) : TEXT("quiet");
		}
		else
		{
			Text += Runner->GetNodeName().ToString();
			if (Runner->IsPaused()) { Text += TEXT(" (held still)"); }
			if (Runner->GetState() == EDnaRunnerState::ShowingLine) { Text += TEXT(": ") + DescribeLine(Runner->GetCurrentLine()); }
			if (Waiting > 0) { Text += FString::Printf(TEXT("  +%d waiting"), Waiting); }
		}
		TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[SNew(STextBlock).Text(FText::FromString(Text))];
		if (Runner && !Runner->IsPaused() && Runner->GetState() == EDnaRunnerState::ShowingLine)
		{
			Row->AddSlot().AutoWidth().Padding(2.f)
			[
				SNew(SButton).Text(LOCTEXT("Next", "Next")).OnClicked_Lambda([this, WeakStory, Channel]()
				{
					if (UDreamNarrativeStory* Live = WeakStory.Get())
					{
						if (UDreamNarrativeDialogueRunner* Each = Live->GetRunner(Channel)) { Each->Advance(); }
					}
					Refresh();
					return FReply::Handled();
				})
			];
		}
		if (Runner && !Runner->IsPaused() && Runner->GetState() == EDnaRunnerState::AwaitingChoice)
		{
			for (const FDnaChoiceView& Choice : Runner->GetChoices())
			{
				const int32 Index = Choice.Index;
				Row->AddSlot().AutoWidth().Padding(2.f)
				[
					SNew(SButton).Text(Choice.Label).OnClicked_Lambda([this, WeakStory, Channel, Index]()
					{
						if (UDreamNarrativeStory* Live = WeakStory.Get())
						{
							if (UDreamNarrativeDialogueRunner* Each = Live->GetRunner(Channel)) { Each->Choose(Index); }
						}
						Refresh();
						return FReply::Handled();
					})
				];
			}
		}
		ChannelBox->AddSlot().AutoHeight().Padding(1.f)[Row];
	}
}

TSharedRef<ITableRow> SDnaDebugPanel::MakeTextRow(TSharedPtr<FString> Item, const TSharedRef<STableViewBase>& Owner)
{
	return SNew(STableRow<TSharedPtr<FString>>, Owner)[SNew(STextBlock).Text(FText::FromString(*Item))];
}

#undef LOCTEXT_NAMESPACE
