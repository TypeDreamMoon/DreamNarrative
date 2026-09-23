// Copyright DreamMoon. All Rights Reserved.

#include "Spec/DnaSpecSuite.h"
#include "DreamNarrativeLedger.h"
#include "Fixtures/DnaTestWorld.h"
#include "HAL/FileManager.h"
#include "Lang/DnaLexer.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Spec/DnaTestBlocks.h"

namespace UE::DreamNarrative::Tests::Spec
{
	using namespace UE::DreamNarrative::Lang;

	namespace
	{
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

		TArray<FDnaRawLine> ScanLines(const FString& Path, const FString& Text)
		{
			FDnaScanResult Scan;
			FDnaDiagnosticSink Scratch;
			ScanDnaLines(FDnaSourceText(Path, Text), Scan, Scratch);
			return MoveTemp(Scan.Lines);
		}

		/** The files a source's `file` block lists under `with:`, as written. */
		TArray<FString> ScanWithFiles(const FString& Path, const FString& Text)
		{
			TArray<FString> Files;
			bool bInFileBlock = false;
			for (const FDnaRawLine& Line : ScanLines(Path, Text))
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
				List.ReplaceCharInline(TCHAR(0xFF0C), TEXT(','));
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
	}

	FString GetTestsDir()
	{
		const FString PluginDir = GetPluginDir();
		return PluginDir.IsEmpty() ? FString() : PluginDir / TEXT("Tests");
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

	TArray<FString> ScanTestNames(const FString& Relative)
	{
		TArray<FString> Names;
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *(GetTestsDir() / Relative)))
		{
			return Names;
		}
		for (const FDnaRawLine& Line : ScanLines(Relative, Text))
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

	FString ToDisplayPath(const FString& Relative)
	{
		FString Display = FPaths::ChangeExtension(Relative, TEXT(""));
		Display.RemoveFromStart(TEXT("Spec/"));
		Display.ReplaceInline(TEXT("."), TEXT("_"));
		Display.ReplaceInline(TEXT("/"), TEXT("."));
		return Display;
	}

	bool RunSpecTest(FAutomationTestBase& Test, const FString& Relative, const FString& BlockName, IDnaTrace* Trace)
	{
		// The test file, then whatever it pulls in with `with:`.
		TArray<FDnaTestSource> Sources;
		FDnaTestSource Main;
		Main.Path = FPaths::ConvertRelativePathToFull(GetTestsDir() / Relative);
		if (!FFileHelper::LoadFileToString(Main.Text, *Main.Path))
		{
			Test.AddError(FString::Printf(TEXT("Cannot read %s"), *Main.Path));
			return false;
		}
		for (const FString& With : ScanWithFiles(Main.Path, Main.Text))
		{
			FDnaTestSource& Source = Sources.AddDefaulted_GetRef();
			Source.Path = FPaths::ConvertRelativePathToFull(FPaths::GetPath(Main.Path) / With);
			if (!FFileHelper::LoadFileToString(Source.Text, *Source.Path))
			{
				Test.AddError(FString::Printf(TEXT("%s: cannot read '%s', which its file block lists under with:"), *Relative, *With));
				return false;
			}
		}
		Sources.Insert(MoveTemp(Main), 0);

		FDnaTestWorld World(Test);
		World.Ledger().SetTrace(Trace);
		if (!World.Compile(Sources))
		{
			return false;
		}
		const FDnaTestInfo* Info = World.Program().Tests.Find(BlockName);
		if (!Info || !Info->Node)
		{
			Test.AddError(FString::Printf(TEXT("%s has no test called %s"), *Relative, *BlockName));
			return false;
		}
		const bool bOk = RunDnaTestBlock(World, *Info->Node, Relative);
		World.Ledger().SetTrace(nullptr);
		return bOk;
	}
}
