// Copyright DreamMoon. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Lang/DnaDump.h"
#include "Lang/DnaExpr.h"
#include "Lang/DnaLexer.h"
#include "Lang/DnaModel.h"
#include "Lang/DnaParser.h"
#include "Lang/DnaValidator.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

/**
 * The corpus runner.
 *
 * Files under Tests/Corpus are the language's contract. Each fixture carries its own expectations
 * as `// EXPECT DNSnnnn (line,col)` comments, so the source that provokes a diagnostic and the
 * diagnostic it must provoke sit a few lines apart. A fixture with no expectations must parse clean.
 *
 * A fixture may also have a `.tree` sidecar: the DumpDnaTree output it must produce. That is what
 * pins down classification -- a speech line silently becoming narration is a diff, not a green run.
 * When the sidecar is missing the test writes `<name>.tree.actual` next to the fixture for promotion
 * and passes on diagnostics alone; when it mismatches the test fails and writes the same file.
 *
 *   DreamNarrative.Corpus.Parse   -- one test per .dna under Tests/Corpus/Parse
 */
namespace UE::DreamNarrative::Lang::CorpusTests
{
	FString GetCorpusRoot()
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("DreamNarrative"));
		if (!Plugin.IsValid())
		{
			return FString();
		}
		return FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir() / TEXT("Tests/Corpus"));
	}

	void FindCorpusFiles(const TCHAR* SubDirectory, TArray<FString>& OutFiles)
	{
		const FString Root = GetCorpusRoot();
		if (Root.IsEmpty())
		{
			return;
		}
		IFileManager::Get().FindFilesRecursive(OutFiles, *(Root / SubDirectory), TEXT("*.dna"), true, false, false);
		OutFiles.Sort();
	}

	FString ToTestName(const FString& FullPath)
	{
		FString Relative = FullPath;
		FPaths::MakePathRelativeTo(Relative, *(GetCorpusRoot() / TEXT("")));
		return Relative.Replace(TEXT("\\"), TEXT("/"));
	}

	/** Directory separators become hierarchy, remaining dots become underscores, so the leaf keeps its name. */
	FString ToDisplayName(const FString& RelativePath)
	{
		FString Display = RelativePath;
		Display.ReplaceInline(TEXT("."), TEXT("_"));
		Display.ReplaceInline(TEXT("/"), TEXT("."));
		return Display;
	}

	struct FExpectation
	{
		FString Code;
		int32 Line = INDEX_NONE;
		int32 Column = INDEX_NONE;
		bool bMatched = false;

		FString Describe() const
		{
			return Line == INDEX_NONE ? Code : FString::Printf(TEXT("%s at (%d,%d)"), *Code, Line, Column);
		}
	};

	TArray<FExpectation> ParseExpectations(const FString& SourceText)
	{
		TArray<FExpectation> Expectations;
		TArray<FString> Lines;
		SourceText.ParseIntoArrayLines(Lines, false);
		for (const FString& Line : Lines)
		{
			const FString Trimmed = Line.TrimStartAndEnd();
			if (!Trimmed.StartsWith(TEXT("// EXPECT")))
			{
				continue;
			}
			FString Remainder = Trimmed.Mid(9).TrimStartAndEnd();
			FExpectation Expectation;
			int32 Space = INDEX_NONE;
			if (Remainder.FindChar(TEXT(' '), Space))
			{
				Expectation.Code = Remainder.Left(Space);
				Remainder = Remainder.Mid(Space + 1).TrimStartAndEnd();
			}
			else
			{
				Expectation.Code = Remainder;
				Remainder.Empty();
			}
			int32 Open = INDEX_NONE;
			int32 Close = INDEX_NONE;
			if (Remainder.FindChar(TEXT('('), Open) && Remainder.FindChar(TEXT(')'), Close) && Close > Open)
			{
				const FString Inside = Remainder.Mid(Open + 1, Close - Open - 1);
				FString LineText, ColumnText;
				if (Inside.Split(TEXT(","), &LineText, &ColumnText))
				{
					Expectation.Line = FCString::Atoi(*LineText.TrimStartAndEnd());
					Expectation.Column = FCString::Atoi(*ColumnText.TrimStartAndEnd());
				}
			}
			if (!Expectation.Code.IsEmpty())
			{
				Expectations.Add(Expectation);
			}
		}
		return Expectations;
	}

	FString DiffFirstLines(const FString& Expected, const FString& Actual, int32 MaxReported = 6)
	{
		TArray<FString> Left, Right;
		Expected.ParseIntoArrayLines(Left, false);
		Actual.ParseIntoArrayLines(Right, false);
		TArray<FString> Report;
		const int32 Count = FMath::Max(Left.Num(), Right.Num());
		for (int32 Index = 0; Index < Count && Report.Num() < MaxReported; ++Index)
		{
			const FString L = Left.IsValidIndex(Index) ? Left[Index] : TEXT("<missing>");
			const FString R = Right.IsValidIndex(Index) ? Right[Index] : TEXT("<missing>");
			if (L != R)
			{
				Report.Add(FString::Printf(TEXT("  line %d:\n    expected: %s\n    actual:   %s"), Index + 1, *L, *R));
			}
		}
		return Report.Num() > 0 ? FString::Join(Report, TEXT("\n")) : TEXT("  (no differing line -- trailing whitespace?)");
	}

	FString Normalize(FString Text)
	{
		Text.ReplaceInline(TEXT("\r\n"), TEXT("\n"));
		if (!Text.EndsWith(TEXT("\n")))
		{
			Text += TEXT("\n");
		}
		return Text;
	}
}

namespace UE::DreamNarrative::Lang::CorpusTests
{
	/** Runs one fixture: parse, optionally build + validate, then compare against its expectations. */
	bool RunFixture(FAutomationTestBase& Test, const FString& Parameters, bool bValidate, bool bCompareTree)
	{
		const FString FullPath = GetCorpusRoot() / Parameters;
		FString SourceText;
		if (!FFileHelper::LoadFileToString(SourceText, *FullPath))
		{
			Test.AddError(FString::Printf(TEXT("Cannot read fixture %s"), *FullPath));
			return false;
		}

		TArray<FExpectation> Expectations = ParseExpectations(SourceText);

		FDnaDiagnosticSink Diagnostics(FullPath);
		TSharedPtr<FDnaDocument> Document = MakeShared<FDnaDocument>();
		FDnaParseOptions Options;
		ParseDnaDocument(FDnaSourceText(FullPath, SourceText), Options, *Document, Diagnostics);

		FDnaProgram Program;
		if (bValidate)
		{
			BuildDnaProgram({ Document }, Program, Diagnostics);
			ValidateDnaProgram(Program, Diagnostics);
		}

		// -- diagnostics against expectations
		for (const FDnaDiagnostic& Diagnostic : Diagnostics.GetDiagnostics())
		{
			bool bMatched = false;
			for (FExpectation& Expectation : Expectations)
			{
				if (Expectation.bMatched || Expectation.Code != Diagnostic.Code)
				{
					continue;
				}
				if (Expectation.Line != INDEX_NONE && (Expectation.Line != Diagnostic.Span.Line || Expectation.Column != Diagnostic.Span.Column))
				{
					continue;
				}
				Expectation.bMatched = true;
				bMatched = true;
				break;
			}
			if (!bMatched && Diagnostic.Severity != EDnaSeverity::Info)
			{
				Test.AddError(FString::Printf(TEXT("Unexpected diagnostic: %s"), *FDnaDiagnosticSink::ToLogString(Diagnostic)));
			}
		}
		for (const FExpectation& Expectation : Expectations)
		{
			if (!Expectation.bMatched)
			{
				TArray<FString> Actual;
				for (const FDnaDiagnostic& Diagnostic : Diagnostics.GetDiagnostics())
				{
					Actual.Add(FString::Printf(TEXT("%s at (%d,%d)"), *Diagnostic.Code, Diagnostic.Span.Line, Diagnostic.Span.Column));
				}
				Test.AddError(FString::Printf(TEXT("Expected %s but it was not raised. Raised: %s"),
					*Expectation.Describe(), Actual.Num() > 0 ? *FString::Join(Actual, TEXT("; ")) : TEXT("(none)")));
			}
		}

		if (!bCompareTree)
		{
			return !Test.HasAnyErrors();
		}

		// -- tree golden
		const FString TreePath = FPaths::ChangeExtension(FullPath, TEXT("tree"));
		const FString ActualTree = Normalize(DumpDnaTree(*Document));
		FString ExpectedTree;
		if (FFileHelper::LoadFileToString(ExpectedTree, *TreePath))
		{
			ExpectedTree = Normalize(ExpectedTree);
			if (ExpectedTree != ActualTree)
			{
				FFileHelper::SaveStringToFile(ActualTree, *(TreePath + TEXT(".actual")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
				Test.AddError(FString::Printf(TEXT("Tree differs from %s (actual written next to it):\n%s"), *TreePath, *DiffFirstLines(ExpectedTree, ActualTree)));
			}
		}
		else
		{
			FFileHelper::SaveStringToFile(ActualTree, *(TreePath + TEXT(".actual")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
			Test.AddInfo(FString::Printf(TEXT("No .tree golden for %s; wrote %s.actual for promotion"), *Parameters, *TreePath));
		}
		return !Test.HasAnyErrors();
	}

	void ListFixtures(const TCHAR* Suite, TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands)
	{
		TArray<FString> Files;
		FindCorpusFiles(Suite, Files);
		for (const FString& File : Files)
		{
			const FString Relative = ToTestName(File);
			OutBeautifiedNames.Add(ToDisplayName(Relative));
			OutTestCommands.Add(Relative);
		}
	}
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FDnaCorpusParseTest, "DreamNarrative.Corpus.Parse", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

void FDnaCorpusParseTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	UE::DreamNarrative::Lang::CorpusTests::ListFixtures(TEXT("Parse"), OutBeautifiedNames, OutTestCommands);
}

bool FDnaCorpusParseTest::RunTest(const FString& Parameters)
{
	return UE::DreamNarrative::Lang::CorpusTests::RunFixture(*this, Parameters, /*bValidate=*/false, /*bCompareTree=*/true);
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FDnaCorpusValidateTest, "DreamNarrative.Corpus.Validate", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

void FDnaCorpusValidateTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	UE::DreamNarrative::Lang::CorpusTests::ListFixtures(TEXT("Validate"), OutBeautifiedNames, OutTestCommands);
}

bool FDnaCorpusValidateTest::RunTest(const FString& Parameters)
{
	return UE::DreamNarrative::Lang::CorpusTests::RunFixture(*this, Parameters, /*bValidate=*/true, /*bCompareTree=*/false);
}


// ---------------------------------------------------------------- unit checks on the lexical layer

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaLexerTest, "DreamNarrative.Lang.Lexer", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDnaLexerTest::RunTest(const FString& Parameters)
{
	using namespace UE::DreamNarrative::Lang;

	{
		const FDnaSourceText Source(TEXT("mem"), TEXT("换水次数 >= 5 and 梅蕾状态 == 初绽"));
		FDnaDiagnosticSink Sink;
		TArray<FDnaToken> Tokens;
		TokenizeDnaSegment(Source, 0, Source.Len(), Tokens, Sink);
		TestEqual(TEXT("no diagnostics"), Sink.Num(), 0);
		TestEqual(TEXT("token count"), Tokens.Num(), 8);
		TestTrue(TEXT("cjk identifier"), Tokens[0].Is(EDnaTokenKind::Identifier) && Tokens[0].Text == TEXT("换水次数"));
		TestTrue(TEXT("full-width >="), Tokens[1].Is(EDnaTokenKind::GreaterEqual));
		TestTrue(TEXT("int"), Tokens[2].Is(EDnaTokenKind::Int) && Tokens[2].Integer == 5);
		TestTrue(TEXT("and keyword"), Tokens[3].IsKeyword(EDnaKeyword::And));
		TestTrue(TEXT("full-width =="), Tokens[5].Is(EDnaTokenKind::EqualEqual));
	}
	{
		const FDnaSourceText Source(TEXT("mem"), TEXT("0.2s 500ms 20m 2h 30% 1.9s..2.1s"));
		FDnaDiagnosticSink Sink;
		TArray<FDnaToken> Tokens;
		TokenizeDnaSegment(Source, 0, Source.Len(), Tokens, Sink);
		TestEqual(TEXT("no diagnostics"), Sink.Num(), 0);
		TestTrue(TEXT("0.2s"), Tokens[0].Is(EDnaTokenKind::Duration) && FMath::IsNearlyEqual(Tokens[0].Real, 0.2));
		TestTrue(TEXT("500ms"), Tokens[1].Is(EDnaTokenKind::Duration) && FMath::IsNearlyEqual(Tokens[1].Real, 0.5));
		TestTrue(TEXT("20m"), Tokens[2].Is(EDnaTokenKind::Duration) && FMath::IsNearlyEqual(Tokens[2].Real, 1200.0));
		TestTrue(TEXT("2h"), Tokens[3].Is(EDnaTokenKind::Duration) && FMath::IsNearlyEqual(Tokens[3].Real, 7200.0));
		TestTrue(TEXT("30%"), Tokens[4].Is(EDnaTokenKind::Percent) && FMath::IsNearlyEqual(Tokens[4].Real, 30.0));
		TestTrue(TEXT("range start"), Tokens[5].Is(EDnaTokenKind::Duration));
		TestTrue(TEXT("dotdot"), Tokens[6].Is(EDnaTokenKind::DotDot));
		TestTrue(TEXT("range end"), Tokens[7].Is(EDnaTokenKind::Duration));
	}
	{
		const FDnaSourceText Source(TEXT("mem"), TEXT("凛: 她说 // 注释\n「引号//不是注释」 // 真注释"));
		FDnaScanResult Scan;
		FDnaDiagnosticSink Sink;
		ScanDnaLines(Source, Scan, Sink);
		TestEqual(TEXT("two lines"), Scan.Lines.Num(), 2);
		TestEqual(TEXT("content before comment"), Scan.Lines[0].Content, FString(TEXT("凛: 她说")));
		TestEqual(TEXT("comment kept"), Scan.Lines[0].Comment, FString(TEXT("// 注释")));
		TestEqual(TEXT("quoted slashes are text"), Scan.Lines[1].Content, FString(TEXT("「引号//不是注释」")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaExprTest, "DreamNarrative.Lang.Expr", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDnaExprTest::RunTest(const FString& Parameters)
{
	using namespace UE::DreamNarrative::Lang;

	auto Parse = [this](const TCHAR* Text) -> FString
	{
		const FDnaSourceText Source(TEXT("mem"), Text);
		FDnaDiagnosticSink Sink;
		FDnaExprPtr Expr = ParseDnaExpressionSegment(Source, 0, Source.Len(), Sink);
		if (!Expr.IsValid())
		{
			return Sink.Num() > 0 ? FDnaDiagnosticSink::ToWireString(Sink.GetDiagnostics()[0]) : TEXT("<null>");
		}
		return Expr->ToString();
	};

	TestEqual(TEXT("precedence"), Parse(TEXT("a or b and not c == 1 + 2 * 3")), FString(TEXT("(a or (b and (not (c == (1 + (2 * 3))))))")));
	TestEqual(TEXT("modulo"), Parse(TEXT("周期 % 3 == 0")), FString(TEXT("((周期 % 3) == 0)")));
	TestEqual(TEXT("dotted and call"), Parse(TEXT("猎狼.completed and visited(凛_日常) == 0")), FString(TEXT("(猎狼.completed and (visited(凛_日常) == 0))")));
	TestEqual(TEXT("in list"), Parse(TEXT("梅蕾状态 in (初绽, 盛开)")), FString(TEXT("(梅蕾状态 in (初绽, 盛开))")));
	TestEqual(TEXT("chapter keyword"), Parse(TEXT("chapter == 第一章")), FString(TEXT("(chapter == 第一章)")));
	TestEqual(TEXT("negative literal folds"), Parse(TEXT("x > -3")), FString(TEXT("(x > -3)")));
	TestEqual(TEXT("percent literal"), Parse(TEXT("chance(30%)")), FString(TEXT("chance(0.3)")));
	TestEqual(TEXT("missing operand"), Parse(TEXT("a >")), FString(TEXT("DNS2007: Expected a value, found end of line")));
	TestEqual(TEXT("trailing"), Parse(TEXT("1 2")), FString(TEXT("DNS2008: Unexpected '2' after the expression")));
	return true;
}

#endif
