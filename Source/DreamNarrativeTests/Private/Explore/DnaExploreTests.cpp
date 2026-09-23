// Copyright DreamMoon. All Rights Reserved.
//
//   DreamNarrative.Explore.<path>.<name>          one test per `explore` block under Tests/Spec and Tests/Samples
//   DreamNarrative.Explore.Corpus.<file>.<name>   the explorer's own fixtures, Tests/Corpus/Explore: a block must
//                                                 find exactly the codes the `// EXPECT` lines above it list, and
//                                                 every repro it prints must fail when run on its own
//   DreamNarrative.Explore.ReportsWhatAMoveLogs   what a move logs is reported once, with the way to it

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DreamNarrativeLedger.h"
#include "Explore/DnaExploreBlocks.h"
#include "Fixtures/DnaLogCapture.h"
#include "Fixtures/DnaTestWorld.h"
#include "Internationalization/Regex.h"
#include "Lang/DnaLexer.h"
#include "Lang/DnaModel.h"
#include "Misc/FileHelper.h"
#include "Spec/DnaSpecSuite.h"
#include "Spec/DnaTestBlocks.h"

DEFINE_LOG_CATEGORY_STATIC(LogDnaExploreTests, Log, All);

/** The explorer reports what the story logs, once and with the way there; the log does not also fail the test line by line. */
class FDnaExploreTestBase : public FAutomationTestBase
{
public:
	FDnaExploreTestBase(const FString& InName, bool bInComplexTask)
		: FAutomationTestBase(InName, bInComplexTask)
	{
	}

	virtual bool SuppressLogErrors() override { return true; }
	virtual bool SuppressLogWarnings() override { return true; }
};

namespace UE::DreamNarrative::Tests::Explore
{
	using namespace UE::DreamNarrative::Lang;

	/** The explore block called Name, or null after an error. */
	const FDnaLineNode* FindExploreBlock(FAutomationTestBase& Test, const FDnaTestWorld& World, const FString& Relative, const FString& Name)
	{
		const FDnaTestInfo* Info = World.Program().Explores.Find(Name);
		if (!Info || !Info->Node)
		{
			Test.AddError(FString::Printf(TEXT("%s has no explore block called %s"), *Relative, *Name));
			return nullptr;
		}
		return Info->Node;
	}

	/** The codes the `// EXPECT` comment lines right above the block's header list, sorted. */
	TArray<FString> ReadExpectedCodes(const FString& Relative, const FString& BlockName)
	{
		TArray<FString> Codes;
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *(Spec::GetTestsDir() / Relative)))
		{
			return Codes;
		}
		FDnaScanResult Scan;
		FDnaDiagnosticSink Scratch;
		ScanDnaLines(FDnaSourceText(Relative, Text), Scan, Scratch);
		const FString Header = TEXT("explore ") + BlockName;
		for (int32 Index = 0; Index < Scan.Lines.Num(); ++Index)
		{
			const FDnaRawLine& Line = Scan.Lines[Index];
			if (Line.IndentLevel != 0 || !(Line.Content == Header || Line.Content.StartsWith(Header + TEXT(" "))))
			{
				continue;
			}
			for (int32 Above = Index - 1; Above >= 0 && Scan.Lines[Above].IsCommentOnly(); --Above)
			{
				const FString& Comment = Scan.Lines[Above].Comment;
				if (!Comment.Contains(TEXT("EXPECT")))
				{
					continue;
				}
				FRegexMatcher Matcher(FRegexPattern(TEXT("DNS[0-9]{4}")), Comment);
				while (Matcher.FindNext())
				{
					Codes.Add(Matcher.GetCaptureGroup(0));
				}
			}
			break;
		}
		Codes.Sort();
		return Codes;
	}

	/** Runs a repro block on its own, appended to the file it came from. True when it fails, as a repro must. */
	bool ReproFails(FAutomationTestBase& Test, const FString& Relative, const FString& Repro, FString& OutFirstFailure)
	{
		FDnaTestWorld World(Test);
		if (!Spec::CompileTestFile(World, Relative, Repro))
		{
			OutFirstFailure = TEXT("it does not compile");
			return false;
		}
		FString Kind;
		FString Name;
		Repro.Left(Repro.Find(TEXT("\n"))).Split(TEXT(" "), &Kind, &Name);

		TArray<FString> Failures;
		World.CaptureErrors(&Failures);
		{
			FDnaLogCapture Capture;
			if (Kind == TEXT("test"))
			{
				const FDnaTestInfo* Info = World.Program().Tests.Find(Name);
				if (Info && Info->Node)
				{
					RunDnaTestBlock(World, *Info->Node, Relative);
				}
				else
				{
					Failures.Add(TEXT("there is no test called ") + Name);
				}
			}
			else if (const FDnaLineNode* Block = FindExploreBlock(Test, World, Relative, Name))
			{
				FDnaExploreBlockResult Result;
				RunDnaExploreBlock(World, *Block, Relative, Result);
				for (const FDnaExploreFinding& Finding : Result.Findings)
				{
					if (!Finding.bWarning)
					{
						Failures.Add(Finding.Code + TEXT(": ") + Finding.Message);
					}
				}
			}
			Failures.Append(Capture.Take());
		}
		World.CaptureErrors(nullptr);
		OutFirstFailure = Failures.Num() > 0 ? Failures[0] : FString();
		return Failures.Num() > 0;
	}
}

// ---------------------------------------------------------------- the suite's explore blocks

IMPLEMENT_CUSTOM_COMPLEX_AUTOMATION_TEST(FDnaExploreTest, FDnaExploreTestBase, "DreamNarrative.Explore", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

void FDnaExploreTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	using namespace UE::DreamNarrative::Tests::Spec;
	TArray<FString> Files;
	FindTestFiles(Files);
	for (const FString& Relative : Files)
	{
		for (const FString& Name : ScanBlockNames(Relative, TEXT("explore")))
		{
			OutBeautifiedNames.Add(ToDisplayPath(Relative) + TEXT(".") + Name);
			OutTestCommands.Add(Relative + TEXT("|") + Name);
		}
	}
}

bool FDnaExploreTest::RunTest(const FString& Parameters)
{
	using namespace UE::DreamNarrative::Tests;
	using namespace UE::DreamNarrative::Tests::Explore;

	FString Relative, BlockName;
	if (!Parameters.Split(TEXT("|"), &Relative, &BlockName))
	{
		AddError(FString::Printf(TEXT("Malformed test command '%s'"), *Parameters));
		return false;
	}
	FDnaTestWorld World(*this);
	if (!Spec::CompileTestFile(World, Relative))
	{
		return false;
	}
	const UE::DreamNarrative::Lang::FDnaLineNode* Block = FindExploreBlock(*this, World, Relative, BlockName);
	if (!Block)
	{
		return false;
	}
	FDnaExploreBlockResult Result;
	RunDnaExploreBlock(World, *Block, Relative, Result);
	ReportExploreResult(*this, Result);
	return !HasAnyErrors();
}

// ---------------------------------------------------------------- the explorer's fixtures

IMPLEMENT_CUSTOM_COMPLEX_AUTOMATION_TEST(FDnaExploreCorpusTest, FDnaExploreTestBase, "DreamNarrative.Explore.Corpus", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

void FDnaExploreCorpusTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	using namespace UE::DreamNarrative::Tests::Spec;
	TArray<FString> Files;
	FindFilesUnder(TEXT("Corpus/Explore"), Files);
	for (const FString& Relative : Files)
	{
		FString Display = FPaths::ChangeExtension(Relative, TEXT(""));
		Display.RemoveFromStart(TEXT("Corpus/Explore/"));
		Display.ReplaceInline(TEXT("."), TEXT("_"));
		Display.ReplaceInline(TEXT("/"), TEXT("."));
		for (const FString& Name : ScanBlockNames(Relative, TEXT("explore")))
		{
			OutBeautifiedNames.Add(Display + TEXT(".") + Name);
			OutTestCommands.Add(Relative + TEXT("|") + Name);
		}
	}
}

bool FDnaExploreCorpusTest::RunTest(const FString& Parameters)
{
	using namespace UE::DreamNarrative::Tests;
	using namespace UE::DreamNarrative::Tests::Explore;

	FString Relative, BlockName;
	if (!Parameters.Split(TEXT("|"), &Relative, &BlockName))
	{
		AddError(FString::Printf(TEXT("Malformed test command '%s'"), *Parameters));
		return false;
	}
	FDnaTestWorld World(*this);
	if (!Spec::CompileTestFile(World, Relative))
	{
		return false;
	}
	const UE::DreamNarrative::Lang::FDnaLineNode* Block = FindExploreBlock(*this, World, Relative, BlockName);
	if (!Block)
	{
		return false;
	}
	FDnaExploreBlockResult Result;
	RunDnaExploreBlock(World, *Block, Relative, Result);

	// -- the codes found are the codes expected
	TArray<FString> Found;
	for (const FDnaExploreFinding& Finding : Result.Findings)
	{
		Found.Add(Finding.Code);
	}
	Found.Sort();
	const TArray<FString> Expected = ReadExpectedCodes(Relative, BlockName);
	if (Found != Expected)
	{
		AddError(FString::Printf(TEXT("%s: expected [%s], the explorer found [%s]"), *BlockName,
			*FString::Join(Expected, TEXT(", ")), *FString::Join(Found, TEXT(", "))));
		for (int32 Index = 0; Index < Result.Findings.Num(); ++Index)
		{
			AddInfo(Result.Findings[Index].Code + TEXT(": ") + Result.Findings[Index].Message
				+ (Result.Repros[Index].IsEmpty() ? FString() : TEXT("\n\n") + Result.Repros[Index]));
		}
	}

	// -- every repro it printed fails on its own
	for (int32 Index = 0; Index < Result.Findings.Num(); ++Index)
	{
		if (Result.Findings[Index].bWarning || Result.Repros[Index].IsEmpty())
		{
			continue;
		}
		FString FirstFailure;
		if (ReproFails(*this, Relative, Result.Repros[Index], FirstFailure))
		{
			AddInfo(FString::Printf(TEXT("%s's repro fails on its own, as it should: %s\n\n%s"), *Result.Findings[Index].Code, *FirstFailure, *Result.Repros[Index]));
		}
		else
		{
			AddError(FString::Printf(TEXT("%s's repro does not fail when run on its own%s:\n\n%s"), *Result.Findings[Index].Code,
				FirstFailure.IsEmpty() ? TEXT("") : *(TEXT(" (") + FirstFailure + TEXT(")")), *Result.Repros[Index]));
		}
	}
	return !HasAnyErrors();
}

// ---------------------------------------------------------------- what a move logs

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDnaExploreReportsWhatAMoveLogsTest, FDnaExploreTestBase, "DreamNarrative.Explore.ReportsWhatAMoveLogs", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDnaExploreReportsWhatAMoveLogsTest::RunTest(const FString& Parameters)
{
	using namespace UE::DreamNarrative::Tests;
	using namespace UE::DreamNarrative::Tests::Explore;

	FDnaTestWorld World(*this);
	const bool bCompiled = World.CompileText(TEXT(
		"character 甲\n"
		"fact 坏了: bool\n"
		"dialogue 弄坏 for 甲\n"
		"  甲: 要弄坏它吗?\n"
		"  - 弄坏它\n"
		"      set 坏了 = true\n"
		"  - 算了\n"
		"explore 看看\n"
		"  depth 5\n"));
	if (!bCompiled)
	{
		return false;
	}

	// Game code that logs an error when the fact turns true, the way a broken listener would.
	const FDelegateHandle Handle = World.Ledger().OnBatchCommitted.AddLambda([&World](const TArray<FName>& Changed)
	{
		if (Changed.Contains(FName(TEXT("坏了"))) && World.Ledger().GetFactAsBool(TEXT("坏了")))
		{
			UE_LOG(LogDnaExploreTests, Error, TEXT("a listener broke on 坏了"));
		}
	});
	FDnaExploreBlockResult Result;
	RunDnaExploreBlock(World, *World.Program().Explores.FindChecked(TEXT("看看")).Node, TEXT("Inline.dna"), Result);
	World.Ledger().OnBatchCommitted.Remove(Handle);

	TArray<FString> Codes;
	for (const FDnaExploreFinding& Finding : Result.Findings) { Codes.Add(Finding.Code); }
	TestEqual(TEXT("one finding, a logged error"), FString::Join(Codes, TEXT(", ")), FString(TEXT("DNS5101")));
	if (Result.Findings.Num() == 1)
	{
		TestTrue(TEXT("it quotes the line"), Result.Findings[0].Message.Contains(TEXT("a listener broke on 坏了")));
		TestEqual(TEXT("and gives the way there"), Result.Repros[0], FString(TEXT("test 看看_报错\n  talk 甲\n    - 弄坏它")));
	}
	TestTrue(TEXT("explored to the end"), Result.Stats.bExhausted);
	return !HasAnyErrors();
}

#endif
