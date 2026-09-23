// Copyright DreamMoon. All Rights Reserved.
//
// Every `test` block under the plugin's Tests/Spec and Tests/Samples becomes one automation test:
//
//   DreamNarrative.Spec.<path below Tests/Spec, or Samples/...>.<test name>
//
// A test file is compiled on its own, together with the files its `file` block lists under `with:`
// (paths relative to the test file), so a test can drive a sample story without copying it.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Fixtures/DnaTestWorld.h"
#include "HAL/FileManager.h"
#include "Lang/DnaLexer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Spec/DnaTestBlocks.h"

namespace UE::DreamNarrative::Tests::Spec
{
	using namespace UE::DreamNarrative::Lang;

	FString GetTestsDir()
	{
		const FString PluginDir = GetPluginDir();
		return PluginDir.IsEmpty() ? FString() : PluginDir / TEXT("Tests");
	}

	/** The run of identifier characters at Index. */
	FString WordAt(const FString& Text, int32 Index)
	{
		int32 End = Index;
		if (End < Text.Len() && IsDnaIdentifierStart(Text[End]))
		{
			while (End < Text.Len() && IsDnaIdentifierChar(Text[End])) { ++End; }
		}
		return Text.Mid(Index, End - Index);
	}

	int32 SkipSpaces(const FString& Text, int32 Index)
	{
		while (Index < Text.Len() && IsDnaSpace(Text[Index])) { ++Index; }
		return Index;
	}

	/** The names of the `test` blocks in a source, without a full parse. */
	TArray<FString> ScanTestNames(const FDnaSourceText& Source)
	{
		FDnaScanResult Scan;
		FDnaDiagnosticSink Scratch;
		ScanDnaLines(Source, Scan, Scratch);
		TArray<FString> Names;
		for (const FDnaRawLine& Line : Scan.Lines)
		{
			if (Line.IsFloating() || Line.IndentLevel != 0 || WordAt(Line.Content, 0) != TEXT("test"))
			{
				continue;
			}
			const FString Name = WordAt(Line.Content, SkipSpaces(Line.Content, 4));
			if (!Name.IsEmpty())
			{
				Names.Add(Name);
			}
		}
		return Names;
	}

	/** The files a source's `file` block lists under `with:`, as written. */
	TArray<FString> ScanWithFiles(const FDnaSourceText& Source)
	{
		FDnaScanResult Scan;
		FDnaDiagnosticSink Scratch;
		ScanDnaLines(Source, Scan, Scratch);
		TArray<FString> Files;
		bool bInFileBlock = false;
		for (const FDnaRawLine& Line : Scan.Lines)
		{
			if (Line.IsFloating())
			{
				continue;
			}
			if (Line.IndentLevel == 0)
			{
				bInFileBlock = WordAt(Line.Content, 0) == TEXT("file");
				continue;
			}
			if (!bInFileBlock || WordAt(Line.Content, 0) != TEXT("with"))
			{
				continue;
			}
			const int32 Colon = SkipSpaces(Line.Content, 4);
			if (Colon >= Line.Content.Len() || NormalizeDnaPunctuation(Line.Content[Colon]) != TEXT(':'))
			{
				continue;
			}
			FString List = Line.Content.Mid(Colon + 1);
			List.ReplaceInline(TEXT("，"), TEXT(","));
			TArray<FString> Parts;
			List.ParseIntoArray(Parts, TEXT(","), /*bCullEmpty=*/true);
			for (FString& Part : Parts)
			{
				Part.TrimStartAndEndInline();
				if (!Part.IsEmpty()) { Files.Add(Part); }
			}
		}
		return Files;
	}

	/** `Spec/Dialogue/Choices.dna` -> `Dialogue.Choices`; `Samples/Ring.dna` -> `Samples.Ring`. */
	FString ToDisplayPath(const FString& Relative)
	{
		FString Display = FPaths::ChangeExtension(Relative, TEXT(""));
		Display.RemoveFromStart(TEXT("Spec/"));
		Display.ReplaceInline(TEXT("."), TEXT("_"));
		Display.ReplaceInline(TEXT("/"), TEXT("."));
		return Display;
	}

	void FindTestFiles(TArray<FString>& OutRelative)
	{
		const FString TestsDir = GetTestsDir();
		if (TestsDir.IsEmpty())
		{
			return;
		}
		for (const TCHAR* Suite : { TEXT("Spec"), TEXT("Samples") })
		{
			TArray<FString> Files;
			IFileManager::Get().FindFilesRecursive(Files, *(TestsDir / Suite), TEXT("*.dna"), true, false, false);
			Files.Sort();
			for (FString& File : Files)
			{
				FPaths::MakePathRelativeTo(File, *(TestsDir / TEXT("")));
				OutRelative.Add(File.Replace(TEXT("\\"), TEXT("/")));
			}
		}
	}
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FDnaSpecTest, "DreamNarrative.Spec", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

void FDnaSpecTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	using namespace UE::DreamNarrative::Tests::Spec;
	TArray<FString> Files;
	FindTestFiles(Files);
	for (const FString& Relative : Files)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *(GetTestsDir() / Relative)))
		{
			continue;
		}
		for (const FString& Name : ScanTestNames(FDnaSourceText(Relative, Text)))
		{
			OutBeautifiedNames.Add(ToDisplayPath(Relative) + TEXT(".") + Name);
			OutTestCommands.Add(Relative + TEXT("|") + Name);
		}
	}
}

bool FDnaSpecTest::RunTest(const FString& Parameters)
{
	using namespace UE::DreamNarrative::Tests;
	using namespace UE::DreamNarrative::Tests::Spec;

	FString Relative, BlockName;
	if (!Parameters.Split(TEXT("|"), &Relative, &BlockName))
	{
		AddError(FString::Printf(TEXT("Malformed test command '%s'"), *Parameters));
		return false;
	}

	// The test file, then whatever it pulls in with `with:`, each read once.
	const FString TestsDir = GetTestsDir();
	TArray<FDnaTestSource> Sources;
	FDnaTestSource& Main = Sources.AddDefaulted_GetRef();
	Main.Path = FPaths::ConvertRelativePathToFull(TestsDir / Relative);
	if (!FFileHelper::LoadFileToString(Main.Text, *Main.Path))
	{
		AddError(FString::Printf(TEXT("Cannot read %s"), *Main.Path));
		return false;
	}
	// Copied out: adding to Sources below may move the element Main refers to.
	const FString MainDir = FPaths::GetPath(Main.Path);
	const TArray<FString> WithFiles = ScanWithFiles(FDnaSourceText(Main.Path, Main.Text));
	for (const FString& With : WithFiles)
	{
		FDnaTestSource& Source = Sources.AddDefaulted_GetRef();
		Source.Path = FPaths::ConvertRelativePathToFull(MainDir / With);
		if (!FFileHelper::LoadFileToString(Source.Text, *Source.Path))
		{
			AddError(FString::Printf(TEXT("%s: cannot read '%s', which its file block lists under with:"), *Relative, *With));
			return false;
		}
	}

	FDnaTestWorld World(*this);
	if (!World.Compile(Sources))
	{
		return false;
	}
	const UE::DreamNarrative::Lang::FDnaTestInfo* Info = World.Program().Tests.Find(BlockName);
	if (!Info || !Info->Node)
	{
		AddError(FString::Printf(TEXT("%s has no test called %s"), *Relative, *BlockName));
		return false;
	}
	return RunDnaTestBlock(World, *Info->Node, Relative) && !HasAnyErrors();
}

#endif
