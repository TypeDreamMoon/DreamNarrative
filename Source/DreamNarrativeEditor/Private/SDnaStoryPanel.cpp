// Copyright DreamMoon. All Rights Reserved.

#include "SDnaStoryPanel.h"
#include "DreamNarrativeEditorSettings.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Lang/DnaEditSession.h"
#include "Lang/DnaProject.h"
#include "Lang/DnaStaging.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SDnaStoryPanel"

using namespace UE::DreamNarrative;
using namespace UE::DreamNarrative::Graph;
using namespace UE::DreamNarrative::Lang;

namespace
{
	/** Asks for a few values in a small modal window. False when it was cancelled. */
	bool AskFor(const FText& Title, const TArray<FText>& Labels, TArray<FString>& InOutValues, const TSharedPtr<SWidget>& Parent)
	{
		if (!FSlateApplication::IsInitialized())
		{
			return false;
		}
		InOutValues.SetNum(Labels.Num());
		TArray<TSharedPtr<SEditableTextBox>> Boxes;
		bool bAccepted = false;
		TSharedRef<SVerticalBox> Fields = SNew(SVerticalBox);
		for (int32 Index = 0; Index < Labels.Num(); ++Index)
		{
			TSharedPtr<SEditableTextBox> Box;
			Fields->AddSlot().AutoHeight().Padding(4.f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(Labels[Index])]
				+ SVerticalBox::Slot().AutoHeight()[SAssignNew(Box, SEditableTextBox).Text(FText::FromString(InOutValues[Index]))]
			];
			Boxes.Add(Box);
		}
		TSharedRef<SWindow> Window = SNew(SWindow)
			.Title(Title)
			.SizingRule(ESizingRule::Autosized)
			.SupportsMaximize(false)
			.SupportsMinimize(false);
		Window->SetContent(
			SNew(SBox).MinDesiredWidth(420.f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[Fields]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(4.f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().Padding(2.f)
					[
						SNew(SButton).Text(LOCTEXT("Ok", "OK")).OnClicked_Lambda([&bAccepted, &Window]()
						{
							bAccepted = true;
							Window->RequestDestroyWindow();
							return FReply::Handled();
						})
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(2.f)
					[
						SNew(SButton).Text(LOCTEXT("Cancel", "Cancel")).OnClicked_Lambda([&Window]()
						{
							Window->RequestDestroyWindow();
							return FReply::Handled();
						})
					]
				]
			]);
		FSlateApplication::Get().AddModalWindow(Window, Parent);
		if (bAccepted)
		{
			for (int32 Index = 0; Index < Boxes.Num(); ++Index)
			{
				InOutValues[Index] = Boxes[Index]->GetText().ToString();
			}
		}
		return bAccepted;
	}

	FText GroupName(EDnaGraphNodeKind Kind)
	{
		switch (Kind)
		{
		case EDnaGraphNodeKind::Chapter: return LOCTEXT("Chapters", "Chapters");
		case EDnaGraphNodeKind::Quest: return LOCTEXT("Quests", "Quests");
		case EDnaGraphNodeKind::Dialogue: return LOCTEXT("Dialogues", "Dialogues");
		case EDnaGraphNodeKind::Event: return LOCTEXT("Events", "Events");
		case EDnaGraphNodeKind::Stage: return LOCTEXT("Stages", "Stages");
		case EDnaGraphNodeKind::Archive: return LOCTEXT("Archives", "Archives");
		}
		return FText::GetEmpty();
	}
}

void SDnaStoryPanel::Construct(const FArguments& InArgs)
{
	Files = InArgs._Files;
	bProjectFiles = Files.Num() == 0;

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(2.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(2.f)
			[
				SNew(SButton).Text(LOCTEXT("Reload", "Reload")).OnClicked_Lambda([this]() { Reload(); return FReply::Handled(); })
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2.f)
			[
				SNew(SButton).Text(LOCTEXT("Undo", "Undo")).IsEnabled_Lambda([this]() { return UndoFiles.Num() > 0; }).OnClicked_Lambda([this]() { Undo(); return FReply::Handled(); })
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2.f)
			[
				SNew(SButton).Text(LOCTEXT("Redo", "Redo")).IsEnabled_Lambda([this]() { return RedoFiles.Num() > 0; }).OnClicked_Lambda([this]() { Redo(); return FReply::Handled(); })
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2.f)
			[
				SNew(SButton).Text(LOCTEXT("Save", "Save")).IsEnabled_Lambda([this]() { return HasUnsavedEdits(); }).OnClicked_Lambda([this]()
				{
					TArray<FString> Errors;
					if (!SaveAll(Errors)) { Report(FText::FromString(FString::Join(Errors, TEXT("\n")))); }
					return FReply::Handled();
				})
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(2.f)
			[
				SNew(SSearchBox).OnTextChanged_Lambda([this](const FText& Text) { Filter = Text.ToString(); Rebuild(); })
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(4.f, 0.f)
		[
			SAssignNew(Status, STextBlock)
		]
		+ SVerticalBox::Slot().FillHeight(1.f)
		[
			SAssignNew(Tree, STreeView<FItemPtr>)
			.TreeItemsSource(&Roots)
			.OnGenerateRow(this, &SDnaStoryPanel::MakeRow)
			.OnGetChildren(this, &SDnaStoryPanel::GetItemChildren)
			.OnMouseButtonDoubleClick(this, &SDnaStoryPanel::OnOpen)
			.OnContextMenuOpening(this, &SDnaStoryPanel::MakeContextMenu)
		]
	];
	Reload();
}

// ---------------------------------------------------------------- reading

void SDnaStoryPanel::Reload()
{
	if (bProjectFiles)
	{
		FindDnaSources(GetDnaSourceRoot(), Files);
	}
	TMap<FString, FString> Texts;
	for (const TPair<FString, TSharedPtr<FDnaEditSession>>& Pair : Sessions)
	{
		Texts.Add(Pair.Key, Pair.Value->GetText());
	}
	FDnaProgram Program;
	FDnaDiagnosticSink Diagnostics;
	LoadDnaProgram(Files, Program, Diagnostics, &Texts);
	ErrorCount = Diagnostics.NumErrors();
	BuildDnaStoryGraph(Program, Graph);
	Rebuild();
}

void SDnaStoryPanel::Rebuild()
{
	Roots.Reset();
	for (const EDnaGraphNodeKind Kind : { EDnaGraphNodeKind::Chapter, EDnaGraphNodeKind::Quest, EDnaGraphNodeKind::Dialogue,
		EDnaGraphNodeKind::Event, EDnaGraphNodeKind::Stage, EDnaGraphNodeKind::Archive })
	{
		FItemPtr Group = MakeShared<FItem>();
		for (int32 Index = 0; Index < Graph.Nodes.Num(); ++Index)
		{
			const FDnaGraphNode& Node = Graph.Nodes[Index];
			if (Node.Kind != Kind || (!Filter.IsEmpty() && !Node.Name.Contains(Filter)))
			{
				continue;
			}
			FItemPtr Item = MakeShared<FItem>();
			Item->Label = FText::FromString(Node.Name);
			Item->Detail = FText::FromString(Node.Line > 0 ? FString::Printf(TEXT("%s    %s:%d"), *Node.Detail, *FPaths::GetCleanFilename(Node.File), Node.Line) : Node.Detail);
			Item->File = Node.File;
			Item->Line = Node.Line;
			Item->Node = Index;
			auto AddEdge = [&Item, this](const FDnaGraphEdge& Edge, bool bOut)
			{
				const FDnaGraphNode& Other = Graph.Nodes[bOut ? Edge.To : Edge.From];
				FItemPtr Child = MakeShared<FItem>();
				Child->Label = FText::FromString(FString::Printf(TEXT("%s %s %s %s"), bOut ? TEXT("→") : TEXT("←"), LexToString(Edge.Kind), LexToString(Other.Kind), *Other.Name));
				Child->Detail = FText::FromString((Edge.Condition.IsEmpty() ? FString() : TEXT("if ") + Edge.Condition + TEXT("    "))
					+ (Edge.Line > 0 ? FString::Printf(TEXT("%s:%d"), *FPaths::GetCleanFilename(Edge.File), Edge.Line) : FString()));
				Child->File = Edge.File;
				Child->Line = Edge.Line;
				Item->Children.Add(Child);
			};
			for (const FDnaGraphEdge* Edge : Graph.EdgesFrom(Index)) { AddEdge(*Edge, true); }
			for (const FDnaGraphEdge* Edge : Graph.EdgesTo(Index)) { AddEdge(*Edge, false); }
			Group->Children.Add(Item);
		}
		Group->Label = FText::Format(LOCTEXT("Group", "{0} ({1})"), GroupName(Kind), FText::AsNumber(Group->Children.Num()));
		Roots.Add(Group);
	}
	if (Status.IsValid())
	{
		Status->SetText(FText::Format(LOCTEXT("Status", "{0} files · {1} nodes · {2} edges · {3} errors{4}"),
			FText::AsNumber(Files.Num()), FText::AsNumber(Graph.Nodes.Num()), FText::AsNumber(Graph.Edges.Num()), FText::AsNumber(ErrorCount),
			HasUnsavedEdits() ? LOCTEXT("Unsaved", " · unsaved edits") : FText::GetEmpty()));
	}
	if (Tree.IsValid())
	{
		Tree->RequestTreeRefresh();
	}
}

TSharedRef<ITableRow> SDnaStoryPanel::MakeRow(FItemPtr Item, const TSharedRef<STableViewBase>& Owner)
{
	return SNew(STableRow<FItemPtr>, Owner)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(2.f, 1.f)
		[
			SNew(STextBlock).Text(Item->Label)
		]
		+ SHorizontalBox::Slot().FillWidth(1.f).Padding(12.f, 1.f)
		[
			SNew(STextBlock).Text(Item->Detail).ColorAndOpacity(FSlateColor::UseSubduedForeground())
		]
	];
}

void SDnaStoryPanel::GetItemChildren(FItemPtr Item, TArray<FItemPtr>& OutChildren)
{
	OutChildren = Item->Children;
}

void SDnaStoryPanel::OnOpen(FItemPtr Item)
{
	if (Item.IsValid() && !Item->File.IsEmpty() && Item->Line > 0)
	{
		Editor::OpenSource(Item->File, Item->Line);
	}
}

void SDnaStoryPanel::Report(const FText& Error)
{
	if (Status.IsValid())
	{
		Status->SetText(Error);
	}
}

TSharedPtr<SWidget> SDnaStoryPanel::MakeContextMenu()
{
	const TArray<FItemPtr> Selected = Tree.IsValid() ? Tree->GetSelectedItems() : TArray<FItemPtr>();
	const FItemPtr Item = Selected.Num() > 0 ? Selected[0] : nullptr;
	const bool bDialogue = Item.IsValid() && Graph.Nodes.IsValidIndex(Item->Node) && Graph.Nodes[Item->Node].Kind == EDnaGraphNodeKind::Dialogue;
	FMenuBuilder Menu(/*bShouldCloseWindowAfterMenuSelection=*/true, nullptr);
	if (bDialogue)
	{
		const FString Name = Graph.Nodes[Item->Node].Name;
		Menu.AddMenuEntry(LOCTEXT("EditCondition", "Edit condition..."), FText::GetEmpty(), FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this, Name]()
		{
			TArray<FString> Values;
			if (AskFor(LOCTEXT("EditConditionTitle", "Condition (empty for none)"), { FText::FromString(Name) }, Values, AsShared()))
			{
				FText Error;
				if (!EditCondition(Name, Values[0], Error)) { Report(Error); }
			}
		})));
		Menu.AddMenuEntry(LOCTEXT("Connect", "Connect to..."), FText::GetEmpty(), FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this, Name]()
		{
			TArray<FString> Values;
			if (AskFor(LOCTEXT("ConnectTitle", "Jump to which dialogue"), { LOCTEXT("Target", "Dialogue") }, Values, AsShared()))
			{
				FText Error;
				if (!Connect(Name, Values[0], Error)) { Report(Error); }
			}
		})));
		Menu.AddMenuEntry(LOCTEXT("Stage", "Generate staging"), LOCTEXT("StageTip", "Camera shots for each speaker, attached to their lines; again to roll other shots"),
			FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this, Name]()
		{
			FText Error;
			if (!StageDialogue(Name, Error)) { Report(Error); }
		})));
		Menu.AddMenuEntry(LOCTEXT("Remove", "Remove"), FText::GetEmpty(), FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this, Name]()
		{
			FText Error;
			if (!RemoveDialogue(Name, Error)) { Report(Error); }
		})));
	}
	const FString File = Item.IsValid() && !Item->File.IsEmpty() ? Item->File : (Files.Num() > 0 ? Files[0] : FString());
	if (!File.IsEmpty())
	{
		Menu.AddMenuEntry(LOCTEXT("NewDialogue", "New dialogue..."), FText::GetEmpty(), FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this, File]()
		{
			TArray<FString> Values;
			if (AskFor(FText::Format(LOCTEXT("NewDialogueTitle", "New dialogue in {0}"), FText::FromString(FPaths::GetCleanFilename(File))),
				{ LOCTEXT("Name", "Name"), LOCTEXT("Clauses", "for / on / if / channel ..."), LOCTEXT("FirstLine", "First line (角色: 台词)") }, Values, AsShared()))
			{
				FText Error;
				if (!AddDialogue(File, Values[0], Values[1], Values[2], Error)) { Report(Error); }
			}
		})));
	}
	return Menu.MakeWidget();
}

// ---------------------------------------------------------------- edits

FDnaEditSession& SDnaStoryPanel::SessionFor(const FString& File)
{
	TSharedPtr<FDnaEditSession>& Session = Sessions.FindOrAdd(File);
	if (!Session.IsValid())
	{
		Session = MakeShared<FDnaEditSession>();
		FString Text;
		FFileHelper::LoadFileToString(Text, *File);
		Session->Open(File, Text);
	}
	return *Session;
}

FString SDnaStoryPanel::FileOf(const FString& Dialogue) const
{
	const int32 Node = Graph.Find(EDnaGraphNodeKind::Dialogue, Dialogue);
	return Node != INDEX_NONE ? Graph.Nodes[Node].File : FString();
}

bool SDnaStoryPanel::Edited(const FString& File, bool bOk)
{
	if (bOk)
	{
		UndoFiles.Add(File);
		RedoFiles.Reset();
		Reload();
	}
	return bOk;
}

bool SDnaStoryPanel::EditCondition(const FString& Dialogue, const FString& Condition, FText& OutError)
{
	const FString File = FileOf(Dialogue);
	if (File.IsEmpty())
	{
		OutError = FText::Format(LOCTEXT("NoSuchDialogue", "There is no dialogue called {0}"), FText::FromString(Dialogue));
		return false;
	}
	return Edited(File, SessionFor(File).SetDialogueCondition(Dialogue, Condition, OutError));
}

bool SDnaStoryPanel::Connect(const FString& From, const FString& To, FText& OutError)
{
	const FString File = FileOf(From);
	if (File.IsEmpty() || Graph.Find(EDnaGraphNodeKind::Dialogue, To) == INDEX_NONE || Graph.Nodes[Graph.Find(EDnaGraphNodeKind::Dialogue, To)].Line == 0)
	{
		OutError = LOCTEXT("ConnectWhat", "Both ends must be dialogues the project declares");
		return false;
	}
	return Edited(File, SessionFor(File).AddGoTo(From, To, OutError));
}

bool SDnaStoryPanel::AddDialogue(const FString& File, const FString& Name, const FString& Clauses, const FString& FirstLine, FText& OutError)
{
	if (Graph.Find(EDnaGraphNodeKind::Dialogue, Name) != INDEX_NONE)
	{
		OutError = FText::Format(LOCTEXT("Taken", "There is a dialogue called {0} already"), FText::FromString(Name));
		return false;
	}
	TArray<FString> Lines;
	if (!FirstLine.TrimStartAndEnd().IsEmpty()) { Lines.Add(FirstLine.TrimStartAndEnd()); }
	return Edited(File, SessionFor(File).AddDialogue(Name, Clauses.TrimStartAndEnd(), Lines, OutError));
}

bool SDnaStoryPanel::RemoveDialogue(const FString& Dialogue, FText& OutError)
{
	const FString File = FileOf(Dialogue);
	if (File.IsEmpty())
	{
		OutError = FText::Format(LOCTEXT("NoSuchDialogue2", "There is no dialogue called {0}"), FText::FromString(Dialogue));
		return false;
	}
	return Edited(File, SessionFor(File).RemoveDeclaration(EDnaLineKind::Dialogue, Dialogue, OutError));
}

bool SDnaStoryPanel::StageDialogue(const FString& Dialogue, FText& OutError)
{
	const FString File = FileOf(Dialogue);
	if (File.IsEmpty())
	{
		OutError = FText::Format(LOCTEXT("NoSuchDialogue3", "There is no dialogue called {0}"), FText::FromString(Dialogue));
		return false;
	}
	// Each run rolls again: the seed counts the runs for this dialogue.
	int32& Seed = StagingRolls.FindOrAdd(Dialogue);
	return Edited(File, StageDnaDialogue(SessionFor(File), Dialogue, ++Seed, OutError));
}

bool SDnaStoryPanel::Undo()
{
	if (UndoFiles.Num() == 0)
	{
		return false;
	}
	const FString File = UndoFiles.Pop();
	SessionFor(File).Undo();
	RedoFiles.Add(File);
	Reload();
	return true;
}

bool SDnaStoryPanel::Redo()
{
	if (RedoFiles.Num() == 0)
	{
		return false;
	}
	const FString File = RedoFiles.Pop();
	SessionFor(File).Redo();
	UndoFiles.Add(File);
	Reload();
	return true;
}

bool SDnaStoryPanel::HasUnsavedEdits() const
{
	for (const TPair<FString, TSharedPtr<FDnaEditSession>>& Pair : Sessions)
	{
		if (Pair.Value->IsDirty())
		{
			return true;
		}
	}
	return false;
}

bool SDnaStoryPanel::SaveAll(TArray<FString>& OutErrors)
{
	for (const TPair<FString, TSharedPtr<FDnaEditSession>>& Pair : Sessions)
	{
		FDnaEditSession& Session = *Pair.Value;
		if (!Session.IsDirty())
		{
			continue;
		}
		// Kept the way it was written: a file that had a byte order mark keeps it.
		TArray<uint8> Bytes;
		FFileHelper::LoadFileToArray(Bytes, *Pair.Key);
		const bool bBom = Bytes.Num() >= 3 && Bytes[0] == 0xEF && Bytes[1] == 0xBB && Bytes[2] == 0xBF;
		if (FFileHelper::SaveStringToFile(Session.GetText(), *Pair.Key, bBom ? FFileHelper::EEncodingOptions::ForceUTF8 : FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			Session.MarkSaved();
		}
		else
		{
			OutErrors.Add(FString::Printf(TEXT("Cannot write %s"), *Pair.Key));
		}
	}
	Rebuild();
	return OutErrors.Num() == 0;
}

#undef LOCTEXT_NAMESPACE
