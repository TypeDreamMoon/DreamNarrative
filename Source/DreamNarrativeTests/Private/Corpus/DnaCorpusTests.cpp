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
 *   DreamNarrative.Corpus.Parse      -- one test per .dna under Tests/Corpus/Parse
 *   DreamNarrative.Corpus.Validate   -- one test per .dna under Tests/Corpus/Validate, parsed and validated
 */
namespace UE::DreamNarrative::Tests::Corpus
{
	using namespace UE::DreamNarrative::Lang;

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

	/**
	 * The test name under DreamNarrative.Corpus.<Suite>: the path below the suite's directory without
	 * the extension, directories becoming hierarchy and any other dot an underscore.
	 * `Parse/Errors/Indent_Mixed.dna` in the Parse suite is `Errors.Indent_Mixed`.
	 */
	FString ToDisplayName(const FString& RelativePath, const TCHAR* Suite)
	{
		FString Display = FPaths::ChangeExtension(RelativePath, TEXT(""));
		Display.RemoveFromStart(FString(Suite) + TEXT("/"));
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

namespace UE::DreamNarrative::Tests::Corpus
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
			OutBeautifiedNames.Add(ToDisplayName(Relative, Suite));
			OutTestCommands.Add(Relative);
		}
	}
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FDnaCorpusParseTest, "DreamNarrative.Corpus.Parse", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

void FDnaCorpusParseTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	UE::DreamNarrative::Tests::Corpus::ListFixtures(TEXT("Parse"), OutBeautifiedNames, OutTestCommands);
}

bool FDnaCorpusParseTest::RunTest(const FString& Parameters)
{
	return UE::DreamNarrative::Tests::Corpus::RunFixture(*this, Parameters, /*bValidate=*/false, /*bCompareTree=*/true);
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FDnaCorpusValidateTest, "DreamNarrative.Corpus.Validate", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

void FDnaCorpusValidateTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	UE::DreamNarrative::Tests::Corpus::ListFixtures(TEXT("Validate"), OutBeautifiedNames, OutTestCommands);
}

bool FDnaCorpusValidateTest::RunTest(const FString& Parameters)
{
	return UE::DreamNarrative::Tests::Corpus::RunFixture(*this, Parameters, /*bValidate=*/true, /*bCompareTree=*/false);
}

#endif
