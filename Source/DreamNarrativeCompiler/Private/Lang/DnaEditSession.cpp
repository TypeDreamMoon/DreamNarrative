// Copyright DreamMoon. All Rights Reserved.

#include "Lang/DnaEditSession.h"
#include "Lang/DnaLexer.h"
#include "Lang/DnaParser.h"
#include "Lang/DnaPrinter.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"

#define LOCTEXT_NAMESPACE "DnaEditSession"

namespace UE::DreamNarrative::Lang
{
	namespace
	{
		/** A time as a stage writes it: `2.4s` (hundredths at most, one decimal at least) or `34%` / `12.5%`. */
		FString FormatStageTime(double Value, bool bLine)
		{
			if (bLine)
			{
				const double Percent = FMath::RoundToDouble(Value * 1000.0) / 10.0;
				const bool bWhole = FMath::IsNearlyEqual(Percent, FMath::RoundToDouble(Percent), 1e-6);
				return (bWhole ? FString::Printf(TEXT("%d"), static_cast<int32>(FMath::RoundToDouble(Percent))) : FString::Printf(TEXT("%.1f"), Percent)) + TEXT("%");
			}
			FString Text = FString::Printf(TEXT("%.2f"), FMath::RoundToDouble(Value * 100.0) / 100.0);
			if (Text.EndsWith(TEXT("0")))
			{
				Text.LeftChopInline(1);
			}
			return Text + TEXT("s");
		}

		/** Where the pieces of a stage entry's content are: its time, its verb, the span its verb takes. */
		struct FEntryPieces
		{
			int32 VerbStart = INDEX_NONE;
			int32 SpanStart = INDEX_NONE;
			int32 SpanEnd = INDEX_NONE;
			double Span = 0.0;
			bool bRange = false;
		};

		/** Splits a timed entry (`1.9s..2.1s distort ...`) or, bRestore, a restore entry (`camera blend back 0.3s`). */
		bool SplitEntry(const FString& Content, bool bLine, bool bRestore, FEntryPieces& Out)
		{
			TArray<FDnaToken> Tokens;
			FDnaDiagnosticSink Scratch;
			TokenizeDnaSegment(FDnaSourceText(TEXT("entry"), Content), 0, Content.Len(), Tokens, Scratch);
			int32 Index = 1;
			if (!bRestore && Tokens.IsValidIndex(1) && Tokens[1].Is(EDnaTokenKind::DotDot))
			{
				Out.bRange = true;
				Index = 3;
			}
			if (!Tokens.IsValidIndex(Index) || Tokens[Index].Is(EDnaTokenKind::EndOfInput))
			{
				return false;
			}
			Out.VerbStart = Tokens[Index].Span.Offset;
			for (int32 Each = Index + 1; Each < Tokens.Num(); ++Each)
			{
				if (Tokens[Each].Is(bLine ? EDnaTokenKind::Percent : EDnaTokenKind::Duration))
				{
					Out.SpanStart = Tokens[Each].Span.Offset;
					Out.SpanEnd = Tokens[Each].Span.End();
					Out.Span = bLine ? Tokens[Each].Real / 100.0 : Tokens[Each].Real;
					break;
				}
			}
			return true;
		}

		/** Content with its time part replaced (a timed entry), the verb kept in its column when it fits. */
		FString WithTime(const FString& Content, const FEntryPieces& Pieces, const FString& Time)
		{
			const int32 Gap = FMath::Max(1, Pieces.VerbStart - Time.Len());
			return Time + FString::ChrN(Gap, TEXT(' ')) + Content.Mid(Pieces.VerbStart);
		}

		/** Content with its span replaced, or added where the verb takes one. */
		FString WithSpan(const FString& Content, const FEntryPieces& Pieces, const FString& Span)
		{
			if (Pieces.SpanStart != INDEX_NONE)
			{
				return Content.Left(Pieces.SpanStart) + Span + Content.Mid(Pieces.SpanEnd);
			}
			const bool bPush = Content.Mid(Pieces.VerbStart).StartsWith(TEXT("push"));
			return Content.TrimEnd() + (bPush ? TEXT(" over ") : TEXT(" ")) + Span;
		}

		bool IsLineStage(const FDnaLineNode& Stage)
		{
			for (const FDnaLineNodePtr& Child : Stage.Children)
			{
				if (Child.IsValid() && Child->Kind == EDnaLineKind::Property && Child->Payload.Name == TEXT("base") && Child->Payload.Value.TrimStartAndEnd() == TEXT("line"))
				{
					return true;
				}
			}
			return false;
		}
	}

	void FDnaEditSession::Open(const FString& InPath, const FString& InText)
	{
		Path = InPath;
		Text = InText;
		SavedText = InText;
		Undos.Reset();
		Redos.Reset();
		Reparse();
	}

	bool FDnaEditSession::CanSave(FText& OutError) const
	{
		FString DiskText;
		if (!FFileHelper::LoadFileToString(DiskText, *Path))
		{
			OutError = FText::Format(LOCTEXT("ReadFailed", "Cannot read {0}; local edits were kept"), FText::FromString(Path));
			return false;
		}
		if (DiskText != SavedText)
		{
			OutError = FText::Format(LOCTEXT("SaveConflict", "{0} changed on disk. Discard the local edits or overwrite the file; local edits were kept"), FText::FromString(Path));
			return false;
		}
		return true;
	}

	bool FDnaEditSession::Save(FText& OutError, bool bOverwriteExternal)
	{
		if (!IsDirty()) { return true; }
		if (!bOverwriteExternal && !CanSave(OutError))
		{
			return false;
		}
		// A file that had a byte order mark keeps it.
		TArray<uint8> Bytes;
		FFileHelper::LoadFileToArray(Bytes, *Path);
		const bool bBom = Bytes.Num() >= 3 && Bytes[0] == 0xEF && Bytes[1] == 0xBB && Bytes[2] == 0xBF;
		// Written beside the file, then put in its place: a failed write leaves the old text whole.
		const FString Temp = FPaths::GetPath(Path) / FString::Printf(TEXT(".%s.%s.tmp"), *FPaths::GetCleanFilename(Path), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
		if (!FFileHelper::SaveStringToFile(Text, *Temp, bBom ? FFileHelper::EEncodingOptions::ForceUTF8 : FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
			|| !IFileManager::Get().Move(*Path, *Temp, /*bReplace=*/true, /*bEvenIfReadOnly=*/false))
		{
			IFileManager::Get().Delete(*Temp, false, false, true);
			OutError = FText::Format(LOCTEXT("WriteFailed", "Cannot save {0}; local edits were kept"), FText::FromString(Path));
			return false;
		}
		MarkSaved();
		return true;
	}

	bool FDnaEditSession::IsChangedOnDisk() const
	{
		FString DiskText;
		return !FFileHelper::LoadFileToString(DiskText, *Path) || DiskText != SavedText;
	}

	bool FDnaEditSession::Revert(FText& OutError)
	{
		FString DiskText;
		if (!FFileHelper::LoadFileToString(DiskText, *Path))
		{
			OutError = FText::Format(LOCTEXT("RevertFailed", "Cannot read {0}; local edits were kept"), FText::FromString(Path));
			return false;
		}
		Open(Path, DiskText);
		return true;
	}

	void FDnaEditSession::Reparse()
	{
		Document = MakeShared<FDnaDocument>();
		FDnaDiagnosticSink Diagnostics;
		Diagnostics.SetFilePath(Path);
		ParseDnaDocument(FDnaSourceText(Path, Text), FDnaParseOptions(), *Document, Diagnostics);
		ParseErrors = Diagnostics.NumErrors();
	}

	TArray<FString> FDnaEditSession::GetParseErrors() const
	{
		FDnaDocument Scratch;
		FDnaDiagnosticSink Diagnostics;
		Diagnostics.SetFilePath(Path);
		ParseDnaDocument(FDnaSourceText(Path, Text), FDnaParseOptions(), Scratch, Diagnostics);
		TArray<FString> Out;
		for (const FDnaDiagnostic& Diagnostic : Diagnostics.GetDiagnostics())
		{
			if (Diagnostic.Severity == EDnaSeverity::Error)
			{
				Out.Add(FDnaDiagnosticSink::ToLogString(Diagnostic));
			}
		}
		return Out;
	}

	const FDnaLineNode* FDnaEditSession::FindRoot(EDnaLineKind Kind, const FString& Name) const
	{
		for (const FDnaLineNodePtr& Root : Document->Roots)
		{
			if (Root.IsValid() && Root->Kind == Kind && Root->Payload.Name == Name)
			{
				return Root.Get();
			}
		}
		return nullptr;
	}

	bool FDnaEditSession::Apply(TFunctionRef<bool(FDnaSourceEdit&, FText&)> Edit, FText& OutError)
	{
		FDnaSourceEdit Printer(*Document);
		if (!Edit(Printer, OutError))
		{
			return false;
		}
		const FString Before = Text;
		const int32 ErrorsBefore = ParseErrors;
		Text = Printer.Print();
		Reparse();
		if (ParseErrors > ErrorsBefore)
		{
			const TArray<FString> Errors = GetParseErrors();
			OutError = FText::Format(LOCTEXT("WouldNotParse", "The change would not parse: {0}"), FText::FromString(Errors.Num() > 0 ? Errors.Last() : FString()));
			Text = Before;
			Reparse();
			return false;
		}
		Undos.Add(Before);
		Redos.Reset();
		return true;
	}

	bool FDnaEditSession::Undo()
	{
		if (Undos.Num() == 0)
		{
			return false;
		}
		Redos.Add(Text);
		Text = Undos.Pop();
		Reparse();
		return true;
	}

	bool FDnaEditSession::Redo()
	{
		if (Redos.Num() == 0)
		{
			return false;
		}
		Undos.Add(Text);
		Text = Redos.Pop();
		Reparse();
		return true;
	}

	// ---------------------------------------------------------------- operations

	bool FDnaEditSession::AddDialogue(const FString& Name, const FString& Clauses, const TArray<FString>& Lines, FText& OutError)
	{
		if (FindRoot(EDnaLineKind::Dialogue, Name))
		{
			OutError = FText::Format(LOCTEXT("DialogueExists", "There is a dialogue called {0} already"), FText::FromString(Name));
			return false;
		}
		// After the last line that carries anything, with a blank line between.
		int32 After = 0;
		for (const FDnaRawLine& Line : Document->Scan.Lines)
		{
			if (!Line.IsBlank()) { After = Line.LineNumber; }
		}
		return Apply([&](FDnaSourceEdit& Printer, FText&)
		{
			TArray<TPair<int32, FString>> Added;
			if (After > 0) { Added.Emplace(0, FString()); }
			Added.Emplace(0, (TEXT("dialogue ") + Name + TEXT(" ") + Clauses).TrimEnd());
			for (const FString& Line : Lines) { Added.Emplace(1, Line); }
			return Printer.InsertAfter(After, Added);
		}, OutError);
	}

	bool FDnaEditSession::SetDialogueCondition(const FString& Dialogue, const FString& Condition, FText& OutError)
	{
		const FDnaLineNode* Root = FindRoot(EDnaLineKind::Dialogue, Dialogue);
		if (!Root)
		{
			OutError = FText::Format(LOCTEXT("NoDialogue", "There is no dialogue called {0} in this file"), FText::FromString(Dialogue));
			return false;
		}
		const FString& Content = Root->Raw.Content;
		const FString NewCondition = Condition.TrimStartAndEnd();
		FString Rewritten;
		const FDnaExprPtr& Old = Root->Payload.Condition;
		if (Old.IsValid())
		{
			// The `if` and its expression, as offsets into the line's content.
			const int32 ExprStart = Old->Span.Offset - Root->Raw.Span.Offset;
			const int32 ExprEnd = ExprStart + Old->Span.Length;
			int32 IfStart = ExprStart;
			while (IfStart > 0 && Content[IfStart - 1] == TEXT(' ')) { --IfStart; }
			IfStart -= 2;
			if (IfStart < 0 || Content.Mid(IfStart, 2) != TEXT("if"))
			{
				OutError = LOCTEXT("NoIf", "The condition of this header is not written after 'if'");
				return false;
			}
			if (NewCondition.IsEmpty())
			{
				int32 Cut = IfStart;
				while (Cut > 0 && Content[Cut - 1] == TEXT(' ')) { --Cut; }
				Rewritten = Content.Left(Cut) + Content.Mid(ExprEnd);
			}
			else
			{
				Rewritten = Content.Left(ExprStart) + NewCondition + Content.Mid(ExprEnd);
			}
		}
		else if (!NewCondition.IsEmpty())
		{
			Rewritten = Content + TEXT(" if ") + NewCondition;
		}
		else
		{
			return true;
		}
		const int32 Line = Root->LineNumber();
		if (!Apply([&](FDnaSourceEdit& Printer, FText&) { return Printer.SetContent(Line, Rewritten); }, OutError))
		{
			return false;
		}
		// The new condition must read as one.
		const FDnaLineNode* Again = FindRoot(EDnaLineKind::Dialogue, Dialogue);
		if (!NewCondition.IsEmpty() && (!Again || !Again->Payload.Condition.IsValid()))
		{
			Undo();
			Redos.Reset();
			OutError = FText::Format(LOCTEXT("NotACondition", "'{0}' is not a condition"), FText::FromString(NewCondition));
			return false;
		}
		return true;
	}

	bool FDnaEditSession::AddGoTo(const FString& From, const FString& To, FText& OutError)
	{
		const FDnaLineNode* Root = FindRoot(EDnaLineKind::Dialogue, From);
		if (!Root)
		{
			OutError = FText::Format(LOCTEXT("NoFrom", "There is no dialogue called {0} in this file"), FText::FromString(From));
			return false;
		}
		int32 First = 0;
		int32 Last = 0;
		GetDnaNodeLineRange(*Root, First, Last);
		return Apply([&](FDnaSourceEdit& Printer, FText&) { return Printer.InsertAfter(Last, { { 1, TEXT("-> ") + To } }); }, OutError);
	}

	bool FDnaEditSession::RemoveDeclaration(EDnaLineKind Kind, const FString& Name, FText& OutError)
	{
		const FDnaLineNode* Root = FindRoot(Kind, Name);
		if (!Root)
		{
			OutError = FText::Format(LOCTEXT("NoDeclaration", "{0} is not declared in this file"), FText::FromString(Name));
			return false;
		}
		int32 First = 0;
		int32 Last = 0;
		GetDnaNodeLineRange(*Root, First, Last);
		// A blank line before it goes with it when one follows too, so no double blank is left behind.
		const TArray<FDnaRawLine>& Lines = Document->Scan.Lines;
		const bool bBlankBefore = First > 1 && Lines[First - 2].IsBlank();
		const bool bBlankAfter = Last < Lines.Num() && Lines[Last].IsBlank();
		if (bBlankBefore && (bBlankAfter || Last == Lines.Num()))
		{
			--First;
		}
		return Apply([&](FDnaSourceEdit& Printer, FText&) { return Printer.Remove(First, Last); }, OutError);
	}

	// ---------------------------------------------------------------- stages

	const FDnaLineNode* FDnaEditSession::FindLine(int32 Line) const
	{
		const FDnaLineNode* Found = nullptr;
		Document->ForEachNode([&Found, Line](const FDnaLineNode& Node)
		{
			if (!Found && !Node.IsFloating() && Node.LineNumber() == Line)
			{
				Found = &Node;
			}
		});
		return Found;
	}

	const FDnaLineNode* FDnaEditSession::FindStageOf(int32 Line) const
	{
		for (const FDnaLineNode* Node = FindLine(Line); Node; Node = Node->Parent)
		{
			if (Node->Kind == EDnaLineKind::Stage)
			{
				return Node;
			}
		}
		return nullptr;
	}

	bool FDnaEditSession::SetStageEntryTime(int32 Line, double Start, double End, FText& OutError)
	{
		const FDnaLineNode* Node = FindLine(Line);
		const FDnaLineNode* Stage = FindStageOf(Line);
		FEntryPieces Pieces;
		if (!Node || !Stage || Node->Kind != EDnaLineKind::StageEntry || !SplitEntry(Node->Raw.Content, IsLineStage(*Stage), false, Pieces))
		{
			OutError = FText::Format(LOCTEXT("NotAnEntry", "Line {0} is not a timed entry of a stage"), FText::AsNumber(Line));
			return false;
		}
		const bool bLine = IsLineStage(*Stage);
		Start = FMath::Max(Start, 0.0);
		if (Pieces.bRange && End < Start)
		{
			OutError = LOCTEXT("Backwards", "A range cannot end before it starts");
			return false;
		}
		FString Time = FormatStageTime(Start, bLine);
		if (Pieces.bRange)
		{
			Time += TEXT("..") + FormatStageTime(End, bLine);
		}
		const FString Rewritten = WithTime(Node->Raw.Content, Pieces, Time);
		if (Rewritten == Node->Raw.Content)
		{
			return true;
		}
		return Apply([&](FDnaSourceEdit& Printer, FText&) { return Printer.SetContent(Line, Rewritten); }, OutError);
	}

	bool FDnaEditSession::SetStageEntrySpan(int32 Line, double Over, FText& OutError)
	{
		const FDnaLineNode* Node = FindLine(Line);
		const FDnaLineNode* Stage = FindStageOf(Line);
		const bool bRestore = Node && Node->Kind == EDnaLineKind::RestoreEntry;
		FEntryPieces Pieces;
		if (!Node || !Stage || !(bRestore || Node->Kind == EDnaLineKind::StageEntry) || !SplitEntry(Node->Raw.Content, IsLineStage(*Stage), bRestore, Pieces))
		{
			OutError = FText::Format(LOCTEXT("NotAnEntryForSpan", "Line {0} is not an entry of a stage"), FText::AsNumber(Line));
			return false;
		}
		const FString Rewritten = WithSpan(Node->Raw.Content, Pieces, FormatStageTime(FMath::Max(Over, 0.0), IsLineStage(*Stage)));
		if (Rewritten == Node->Raw.Content)
		{
			return true;
		}
		return Apply([&](FDnaSourceEdit& Printer, FText&) { return Printer.SetContent(Line, Rewritten); }, OutError);
	}

	bool FDnaEditSession::AddStageEntry(const FString& StageName, const FString& Track, double Start, double End, bool bRange, const FString& VerbAndArgs, FText& OutError)
	{
		const FDnaLineNode* Stage = FindRoot(EDnaLineKind::Stage, StageName);
		if (!Stage)
		{
			OutError = FText::Format(LOCTEXT("NoStage", "There is no stage called {0} in this file"), FText::FromString(StageName));
			return false;
		}
		const bool bLine = IsLineStage(*Stage);
		FString Time = FormatStageTime(FMath::Max(Start, 0.0), bLine);
		if (bRange)
		{
			Time += TEXT("..") + FormatStageTime(FMath::Max(End, Start), bLine);
		}
		const FDnaLineNode* TrackNode = nullptr;
		for (const FDnaLineNodePtr& Child : Stage->Children)
		{
			if (Child.IsValid() && Child->Kind == EDnaLineKind::Track && Child->Payload.Name == Track)
			{
				TrackNode = Child.Get();
			}
		}
		// The verb lines up with the track's entries when it has some.
		int32 Column = 9;
		if (TrackNode)
		{
			TArray<FDnaLineNode*> Entries;
			TrackNode->GetContentChildren(Entries);
			FEntryPieces Pieces;
			if (Entries.Num() > 0 && SplitEntry(Entries.Last()->Raw.Content, bLine, false, Pieces))
			{
				Column = Pieces.VerbStart;
			}
		}
		const FString Content = Time + FString::ChrN(FMath::Max(1, Column - Time.Len()), TEXT(' ')) + VerbAndArgs.TrimStartAndEnd();
		int32 First = 0;
		int32 Last = 0;
		GetDnaNodeLineRange(TrackNode ? *TrackNode : *Stage, First, Last);
		return Apply([&](FDnaSourceEdit& Printer, FText&)
		{
			if (TrackNode)
			{
				return Printer.InsertAfter(Last, { { 2, Content } });
			}
			return Printer.InsertAfter(Last, { { 1, Track }, { 2, Content } });
		}, OutError);
	}

	bool FDnaEditSession::RemoveStageEntry(int32 Line, FText& OutError)
	{
		const FDnaLineNode* Node = FindLine(Line);
		if (!Node || !(Node->Kind == EDnaLineKind::StageEntry || Node->Kind == EDnaLineKind::RestoreEntry) || !Node->Parent)
		{
			OutError = FText::Format(LOCTEXT("NotAnEntryToRemove", "Line {0} is not an entry of a stage"), FText::AsNumber(Line));
			return false;
		}
		TArray<FDnaLineNode*> Siblings;
		Node->Parent->GetContentChildren(Siblings);
		int32 First = Line;
		int32 Last = Line;
		if (Siblings.Num() == 1)
		{
			// The track (or restore block) would be left with nothing under it: it goes too.
			GetDnaNodeLineRange(*Node->Parent, First, Last);
		}
		return Apply([&](FDnaSourceEdit& Printer, FText&) { return Printer.Remove(First, Last); }, OutError);
	}

	bool FDnaEditSession::SetStageTimeBase(const FString& StageName, bool bLine, double LineSeconds, FText& OutError)
	{
		const FDnaLineNode* Stage = FindRoot(EDnaLineKind::Stage, StageName);
		if (!Stage)
		{
			OutError = FText::Format(LOCTEXT("NoStageForBase", "There is no stage called {0} in this file"), FText::FromString(StageName));
			return false;
		}
		const bool bWasLine = IsLineStage(*Stage);
		if (bWasLine == bLine)
		{
			return true;
		}
		if (LineSeconds <= 0.0)
		{
			OutError = LOCTEXT("NoLength", "Switching the time base needs the length of the line to convert against");
			return false;
		}
		// From the old unit into the new: seconds <-> a fraction of LineSeconds.
		auto Convert = [bLine, LineSeconds](double Old) { return bLine ? Old / LineSeconds : Old * LineSeconds; };
		TArray<TPair<int32, FString>> Rewrites;
		TArray<int32> Removals;
		TFunction<void(const FDnaLineNode&)> Visit = [&](const FDnaLineNode& Node)
		{
			for (const FDnaLineNodePtr& ChildPtr : Node.Children)
			{
				if (!ChildPtr.IsValid() || ChildPtr->IsFloating()) { continue; }
				const FDnaLineNode& Child = *ChildPtr;
				const FDnaLinePayload& P = Child.Payload;
				if (Child.Kind == EDnaLineKind::Duration || (Child.Kind == EDnaLineKind::Property && P.Name == TEXT("base")))
				{
					Removals.Add(Child.LineNumber());
				}
				else if (Child.Kind == EDnaLineKind::StageEntry || Child.Kind == EDnaLineKind::RestoreEntry)
				{
					const bool bRestore = Child.Kind == EDnaLineKind::RestoreEntry;
					FEntryPieces Pieces;
					if (!SplitEntry(Child.Raw.Content, bWasLine, bRestore, Pieces)) { continue; }
					FString Content = Child.Raw.Content;
					if (Pieces.SpanStart != INDEX_NONE)
					{
						Content = WithSpan(Content, Pieces, FormatStageTime(Convert(Pieces.Span), bLine));
					}
					if (!bRestore)
					{
						// The time part is ahead of the span: rewriting it after keeps the span's offsets good.
						const double Scale = bWasLine ? 0.01 : 1.0;
						FString Time = FormatStageTime(Convert(P.TimeStart * Scale), bLine);
						if (P.bIsRange)
						{
							Time += TEXT("..") + FormatStageTime(Convert(P.TimeEnd * Scale), bLine);
						}
						Content = WithTime(Content, Pieces, Time);
					}
					Rewrites.Emplace(Child.LineNumber(), Content);
				}
				Visit(Child);
			}
		};
		Visit(*Stage);
		const int32 Header = Stage->LineNumber();
		const FString Added = bLine ? FString(TEXT("base: line")) : TEXT("duration ") + FormatStageTime(LineSeconds, false);
		return Apply([&](FDnaSourceEdit& Printer, FText&)
		{
			for (const TPair<int32, FString>& Rewrite : Rewrites)
			{
				Printer.SetContent(Rewrite.Key, Rewrite.Value);
			}
			for (const int32 Line : Removals)
			{
				Printer.Remove(Line, Line);
			}
			return Printer.InsertAfter(Header, { { 1, Added } });
		}, OutError);
	}

	bool FDnaEditSession::SetLine(int32 Line, const FString& Content, FText& OutError)
	{
		return Apply([&](FDnaSourceEdit& Printer, FText& Error)
		{
			if (!Printer.SetContent(Line, Content))
			{
				Error = FText::Format(LOCTEXT("NoLine", "There is no line {0}"), FText::AsNumber(Line));
				return false;
			}
			return true;
		}, OutError);
	}
}

#undef LOCTEXT_NAMESPACE
