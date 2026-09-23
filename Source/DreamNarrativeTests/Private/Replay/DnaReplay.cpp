// Copyright DreamMoon. All Rights Reserved.

#include "Replay/DnaReplay.h"
#include "DreamNarrativeRecorder.h"
#include "DreamNarrativeSettings.h"
#include "DreamNarrativeStory.h"
#include "Fixtures/DnaLogCapture.h"
#include "Fixtures/DnaTestWorld.h"
#include "HAL/FileManager.h"
#include "Lang/DnaModel.h"
#include "Lang/DnaSyntax.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Spec/DnaTestBlocks.h"
#include "UObject/StrongObjectPtr.h"

namespace UE::DreamNarrative::Tests::Replay
{
	using namespace UE::DreamNarrative::Lang;

	FString GetContentRoot()
	{
		const FString Root = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / UDreamNarrativeSettings::Get()->SourceDirectory);
		return IFileManager::Get().DirectoryExists(*Root) ? Root : FString();
	}

	TArray<FString> FindProjectTestFiles()
	{
		TArray<FString> Out;
		const FString Root = GetContentRoot();
		if (Root.IsEmpty())
		{
			return Out;
		}
		IFileManager::Get().FindFilesRecursive(Out, *(Root / TEXT("Tests")), TEXT("*.dna"), true, false, false);
		for (FString& File : Out)
		{
			FPaths::MakePathRelativeTo(File, *(Root / TEXT("")));
			File.ReplaceInline(TEXT("\\"), TEXT("/"));
		}
		Out.Sort();
		return Out;
	}

	bool CompileContentRoot(FDnaTestWorld& World)
	{
		const FString Root = GetContentRoot();
		TArray<FString> Files;
		if (!Root.IsEmpty())
		{
			IFileManager::Get().FindFilesRecursive(Files, *Root, TEXT("*.dna"), true, false, false);
		}
		Files.Sort();
		TArray<FDnaTestSource> Sources;
		for (const FString& File : Files)
		{
			FDnaTestSource& Source = Sources.AddDefaulted_GetRef();
			Source.Path = File;
			if (!FFileHelper::LoadFileToString(Source.Text, *File))
			{
				World.ReportError(FString::Printf(TEXT("Cannot read %s"), *File));
				return false;
			}
		}
		return World.Compile(Sources);
	}

	TArray<FString> RunCollecting(FDnaTestWorld& World, const FString& BlockName, const FString& FileLabel)
	{
		TArray<FString> Failures;
		const FDnaTestInfo* Info = World.Program().Tests.Find(BlockName);
		if (!Info || !Info->Node)
		{
			Failures.Add(FString::Printf(TEXT("There is no test called %s"), *BlockName));
			return Failures;
		}
		World.CaptureErrors(&Failures);
		{
			FDnaLogCapture Capture;
			RunDnaTestBlock(World, *Info->Node, FileLabel);
			Failures.Append(Capture.Take());
		}
		World.CaptureErrors(nullptr);
		return Failures;
	}

	FString Rerecord(FDnaTestWorld& World, const FDnaLineNode& TestNode, const FString& FileLabel)
	{
		TStrongObjectPtr<UDreamNarrativeRecorder> Recorder(NewObject<UDreamNarrativeRecorder>(GetTransientPackage()));
		TArray<FString> Ignored;
		World.CaptureErrors(&Ignored);
		RunDnaTestBlock(World, TestNode, FileLabel, [&World, &Recorder, &TestNode](const FDnaLineNode& Step)
		{
			// The steps before the first move only set the start up; the recorder writes the start down itself.
			const EDnaTestStepKind Kind = Step.Payload.TestStep;
			const bool bSetsUp = Kind == EDnaTestStepKind::Given || Kind == EDnaTestStepKind::Seed
				|| Kind == EDnaTestStepKind::Expect || Kind == EDnaTestStepKind::Save;
			if (!bSetsUp && !Recorder->IsRecording())
			{
				Recorder->Begin(&World.Story(), TestNode.Payload.Name);
			}
		});
		World.CaptureErrors(nullptr);
		if (!Recorder->IsRecording())
		{
			Recorder->Begin(&World.Story(), TestNode.Payload.Name);
		}
		return Recorder->End();
	}

	TArray<FString> DiffLines(const FString& Before, const FString& After)
	{
		TArray<FString> A;
		TArray<FString> B;
		Before.ParseIntoArrayLines(A, /*bCullEmpty=*/false);
		After.ParseIntoArrayLines(B, /*bCullEmpty=*/false);
		const int32 N = A.Num();
		const int32 M = B.Num();

		// Longest common subsequence, suffix table.
		TArray<int32> Table;
		Table.SetNumZeroed((N + 1) * (M + 1));
		auto At = [&Table, M](int32 I, int32 J) -> int32& { return Table[I * (M + 1) + J]; };
		for (int32 I = N - 1; I >= 0; --I)
		{
			for (int32 J = M - 1; J >= 0; --J)
			{
				At(I, J) = A[I] == B[J] ? At(I + 1, J + 1) + 1 : FMath::Max(At(I + 1, J), At(I, J + 1));
			}
		}

		TArray<FString> Full;
		TArray<bool> Changed;
		for (int32 I = 0, J = 0; I < N || J < M;)
		{
			if (I < N && J < M && A[I] == B[J])
			{
				Full.Add(TEXT("  ") + A[I++]);
				Changed.Add(false);
				++J;
			}
			else if (I < N && (J == M || At(I + 1, J) >= At(I, J + 1)))
			{
				// What was there goes first, then what is there now.
				Full.Add(TEXT("- ") + A[I++]);
				Changed.Add(true);
			}
			else
			{
				Full.Add(TEXT("+ ") + B[J++]);
				Changed.Add(true);
			}
		}
		if (!Changed.Contains(true))
		{
			return TArray<FString>();
		}

		// Each change with a line either side; the rest folds into "...".
		TArray<FString> Out;
		bool bSkipped = false;
		for (int32 Index = 0; Index < Full.Num(); ++Index)
		{
			const bool bNear = Changed[Index] || (Index > 0 && Changed[Index - 1]) || (Index + 1 < Full.Num() && Changed[Index + 1]);
			if (bNear)
			{
				if (bSkipped)
				{
					Out.Add(TEXT("  ..."));
					bSkipped = false;
				}
				Out.Add(Full[Index]);
			}
			else
			{
				bSkipped = true;
			}
		}
		return Out;
	}
}
