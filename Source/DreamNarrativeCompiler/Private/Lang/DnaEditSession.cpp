// Copyright DreamMoon. All Rights Reserved.

#include "Lang/DnaEditSession.h"
#include "Lang/DnaParser.h"
#include "Lang/DnaPrinter.h"

#define LOCTEXT_NAMESPACE "DnaEditSession"

namespace UE::DreamNarrative::Lang
{
	void FDnaEditSession::Open(const FString& InPath, const FString& InText)
	{
		Path = InPath;
		Text = InText;
		SavedText = InText;
		Undos.Reset();
		Redos.Reset();
		Reparse();
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
