// Copyright DreamMoon. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Lang/DnaParser.h"
#include "Lang/DnaPrinter.h"
#include "Misc/FileHelper.h"
#include "Spec/DnaSpecSuite.h"

namespace UE::DreamNarrative::Tests::PrinterTests
{
	using namespace UE::DreamNarrative::Lang;

	bool Parse(const FString& Path, const FString& Text, FDnaDocument& Out)
	{
		FDnaDiagnosticSink Scratch;
		Scratch.SetFilePath(Path);
		ParseDnaDocument(FDnaSourceText(Path, Text), FDnaParseOptions(), Out, Scratch);
		return true;
	}

	/** Every expression a document holds, in its lines' order. */
	void CollectExpressions(const FDnaDocument& Document, TArray<FDnaExprPtr>& Out)
	{
		Document.ForEachNode([&Out](const FDnaLineNode& Node)
		{
			const FDnaLinePayload& P = Node.Payload;
			for (const FDnaExprPtr& Expr : { P.Condition, P.Expr, P.DefaultExpr, P.Modifiers.Condition })
			{
				if (Expr.IsValid()) { Out.Add(Expr); }
			}
			for (const FDnaTextPart& Part : P.Text.Parts)
			{
				if (Part.IsExpr()) { Out.Add(Part.Expr); }
			}
			for (const FDnaEmitArg& Arg : P.EmitArgs)
			{
				if (Arg.Value.IsValid()) { Out.Add(Arg.Value); }
			}
			for (const FDnaExprPtr& Goal : P.Goals)
			{
				if (Goal.IsValid()) { Out.Add(Goal); }
			}
		});
	}
}

// ---------------------------------------------------------------- round trips over every source the suite has

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaPrinterRoundTripTest, "DreamNarrative.Lang.Printer.RoundTrip", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDnaPrinterRoundTripTest::RunTest(const FString& Parameters)
{
	using namespace UE::DreamNarrative::Tests;
	using namespace UE::DreamNarrative::Tests::PrinterTests;
	using namespace UE::DreamNarrative::Lang;

	TArray<FString> Files;
	Spec::FindFilesUnder(TEXT("Corpus"), Files);
	Spec::FindFilesUnder(TEXT("Spec"), Files);
	Spec::FindFilesUnder(TEXT("Samples"), Files);
	Spec::FindFilesUnder(TEXT("Smoke"), Files);
	Files.RemoveAll([](const FString& File) { return !File.EndsWith(TEXT(".dna")); });
	TestTrue(TEXT("there are sources to print"), Files.Num() > 40);

	int32 Expressions = 0;
	for (const FString& Relative : Files)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *(Spec::GetTestsDir() / Relative)))
		{
			AddError(TEXT("cannot read ") + Relative);
			continue;
		}
		FDnaDocument Document;
		Parse(Relative, Text, Document);

		// -- nothing edited: byte for byte
		FDnaSourceEdit Edit(Document);
		if (Edit.Print() != Text)
		{
			AddError(Relative + TEXT(": printing it unedited changes it"));
			continue;
		}
		// -- every line given its own content again: still byte for byte
		for (int32 Line = 1; Line <= Document.Source.GetLineCount(); ++Line)
		{
			Edit.SetContent(Line, Edit.GetContent(Line));
		}
		if (Edit.Print() != Text)
		{
			AddError(Relative + TEXT(": writing every line's content back as it was changes the text"));
		}

		// -- every expression printed and read back means the same
		TArray<FDnaExprPtr> Found;
		CollectExpressions(Document, Found);
		for (const FDnaExprPtr& Expr : Found)
		{
			const FString Printed = FormatDnaExpr(*Expr);
			FDnaDiagnosticSink Scratch;
			const FDnaSourceText Source(TEXT("expr"), Printed);
			const FDnaExprPtr Again = ParseDnaExpressionSegment(Source, 0, Source.Len(), Scratch);
			++Expressions;
			if (!Again.IsValid() || Again->ToString() != Expr->ToString())
			{
				AddError(FString::Printf(TEXT("%s(%d): `%s` printed as `%s`, which reads back as %s"), *Relative, Expr->Span.Line,
					*Expr->ToString(), *Printed, Again.IsValid() ? *Again->ToString() : TEXT("nothing")));
			}
		}
	}
	AddInfo(FString::Printf(TEXT("%d files, %d expressions"), Files.Num(), Expressions));
	return !HasAnyErrors();
}

// ---------------------------------------------------------------- edits touch only what they change

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaPrinterEditTest, "DreamNarrative.Lang.Printer.Edits", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDnaPrinterEditTest::RunTest(const FString& Parameters)
{
	using namespace UE::DreamNarrative::Tests::PrinterTests;
	using namespace UE::DreamNarrative::Lang;

	// CRLF, a trailing comment, a comment-only line, no newline at the end.
	const FString Source = TEXT(
		"// 头注释\r\n"
		"dialogue 甲_日常 for 甲 if 熟   // 入口\r\n"
		"  甲: 嗯。\r\n"
		"\r\n"
		"  甲: 再见。\r\n"
		"dialogue 乙_日常 for 乙\r\n"
		"  乙: 你好。");
	FDnaDocument Document;
	Parse(TEXT("Edit.dna"), Source, Document);
	{
		FDnaSourceEdit Edit(Document);
		Edit.SetContent(2, TEXT("dialogue 甲_日常 for 甲 if 熟 and 好感 > 3"));
		TestEqual(TEXT("a changed condition keeps the comment and the space before it"), Edit.Print(), FString(TEXT(
			"// 头注释\r\n"
			"dialogue 甲_日常 for 甲 if 熟 and 好感 > 3   // 入口\r\n"
			"  甲: 嗯。\r\n"
			"\r\n"
			"  甲: 再见。\r\n"
			"dialogue 乙_日常 for 乙\r\n"
			"  乙: 你好。")));
	}
	{
		FDnaSourceEdit Edit(Document);
		Edit.InsertAfter(3, { { 1, TEXT("set 熟 = true") } });
		Edit.InsertAfter(7, { { 1, TEXT("乙: 再会。") } });
		TestEqual(TEXT("inserted lines take the file's indentation and line ending; the last line gets one too"), Edit.Print(), FString(TEXT(
			"// 头注释\r\n"
			"dialogue 甲_日常 for 甲 if 熟   // 入口\r\n"
			"  甲: 嗯。\r\n"
			"  set 熟 = true\r\n"
			"\r\n"
			"  甲: 再见。\r\n"
			"dialogue 乙_日常 for 乙\r\n"
			"  乙: 你好。\r\n"
			"  乙: 再会。\r\n")));
	}
	{
		FDnaSourceEdit Edit(Document);
		int32 First = 0;
		int32 Last = 0;
		TArray<FDnaLineNode*> Dialogues;
		Document.GetRootsOfKind(EDnaLineKind::Dialogue, Dialogues);
		GetDnaNodeLineRange(*Dialogues[1], First, Last);
		TestEqual(TEXT("a node covers its lines"), FString::Printf(TEXT("%d..%d"), First, Last), FString(TEXT("6..7")));
		Edit.Remove(First, Last);
		TestEqual(TEXT("removing the last node leaves the text ending as it did"), Edit.Print(), FString(TEXT(
			"// 头注释\r\n"
			"dialogue 甲_日常 for 甲 if 熟   // 入口\r\n"
			"  甲: 嗯。\r\n"
			"\r\n"
			"  甲: 再见。")));
		GetDnaNodeLineRange(*Dialogues[0], First, Last);
		TestEqual(TEXT("blank lines inside a node are its own"), FString::Printf(TEXT("%d..%d"), First, Last), FString(TEXT("2..5")));
	}

	// Tabs: new lines indent with tabs.
	FDnaDocument Tabbed;
	Parse(TEXT("Tabs.dna"), TEXT("dialogue 丙 for 丙\n\t丙: 嗯。\n"), Tabbed);
	FDnaSourceEdit TabEdit(Tabbed);
	TabEdit.InsertAfter(2, { { 1, TEXT("丙: 好。") } });
	TestEqual(TEXT("a tab-indented file gets tabs"), TabEdit.Print(), FString(TEXT("dialogue 丙 for 丙\n\t丙: 嗯。\n\t丙: 好。\n")));

	// Expressions print with the parentheses they need and no more.
	auto Format = [this](const TCHAR* Written)
	{
		FDnaDiagnosticSink Scratch;
		const FDnaSourceText Text(TEXT("expr"), Written);
		const FDnaExprPtr Expr = ParseDnaExpressionSegment(Text, 0, Text.Len(), Scratch);
		return Expr.IsValid() ? FormatDnaExpr(*Expr) : FString(TEXT("(does not parse)"));
	};
	TestEqual(TEXT("not over a comparison"), Format(TEXT("not (a == 1)")), FString(TEXT("not a == 1")));
	TestEqual(TEXT("or inside and"), Format(TEXT("(a or b) and c")), FString(TEXT("(a or b) and c")));
	TestEqual(TEXT("left-associative minus"), Format(TEXT("a - (b - c)")), FString(TEXT("a - (b - c)")));
	TestEqual(TEXT("no extra parentheses"), Format(TEXT("((a - b) - c)")), FString(TEXT("a - b - c")));
	TestEqual(TEXT("in with arithmetic"), Format(TEXT("x + 1 in (2, 3)")), FString(TEXT("x + 1 in (2, 3)")));
	TestEqual(TEXT("percentages as fractions"), Format(TEXT("chance(30%)")), FString(TEXT("chance(0.3)")));
	return !HasAnyErrors();
}

#endif
