// Copyright DreamMoon. All Rights Reserved.

#include "Explore/DnaExploreBlocks.h"
#include "Fixtures/DnaLogCapture.h"
#include "Fixtures/DnaTestWorld.h"
#include "Lang/DnaExpr.h"
#include "Lang/DnaModel.h"
#include "Lang/DnaSyntax.h"
#include "Misc/AutomationTest.h"
#include "Spec/DnaTestBlocks.h"

namespace UE::DreamNarrative::Tests::Explore
{
	using namespace UE::DreamNarrative::Lang;

	namespace
	{
		/** A line and everything under it, two spaces a level from Level. */
		void WriteLines(const FDnaLineNode& Node, int32 Level, TArray<FString>& Out)
		{
			Out.Add(FString::ChrN(Level * 2, TEXT(' ')) + Node.Raw.Content);
			for (const FDnaLineNodePtr& Child : Node.Children)
			{
				if (Child.IsValid() && !Child->IsFloating())
				{
					WriteLines(*Child, Level + 1, Out);
				}
			}
		}

		/** What follows a property's keyword, as written. */
		FString AfterKeyword(const FDnaLineNode& Node)
		{
			const FString& Content = Node.Raw.Content;
			int32 Start = 0;
			while (Start < Content.Len() && !FChar::IsWhitespace(Content[Start])) { ++Start; }
			return Content.Mid(Start).TrimStartAndEnd();
		}

		/** A block to paste that fails the way Finding does, or empty when there is no way to it to give. */
		FString MakeRepro(const FDnaTestWorld& World, const FDnaLineNode& Block, const FDnaExploreFinding& Finding, const TArray<FString>& Steps, const FString& DepthLine)
		{
			if (Finding.ReproSteps.Num() == 0 && Finding.ReproTail.IsEmpty())
			{
				return FString();
			}
			const FString Base = Block.Payload.Name + TEXT("_") + Finding.ReproSuffix;
			FString Name = Base;
			for (int32 Suffix = 2; World.Program().Tests.Contains(Name) || World.Program().Explores.Contains(Name); ++Suffix)
			{
				Name = Base + FString::FromInt(Suffix);
			}
			TArray<FString> Lines;
			Lines.Add(FString::Printf(TEXT("%s %s"), Finding.bReproIsExplore ? TEXT("explore") : TEXT("test"), *Name));
			Lines.Append(Steps);
			if (Finding.bReproIsExplore && !DepthLine.IsEmpty())
			{
				Lines.Add(TEXT("  ") + DepthLine);
			}
			for (const FString& Step : Finding.ReproSteps)
			{
				Lines.Add(TEXT("  ") + Step);
			}
			if (!Finding.ReproTail.IsEmpty())
			{
				Lines.Add(TEXT("  ") + Finding.ReproTail);
			}
			return FString::Join(Lines, TEXT("\n"));
		}
	}

	void RunDnaExploreBlock(FDnaTestWorld& World, const FDnaLineNode& Block, const FString& FileLabel, FDnaExploreBlockResult& Out)
	{
		// -- the block's steps: where the exploration starts
		{
			FDnaLogCapture Capture;
			Out.bStepsOk = RunDnaTestBlock(World, Block, FileLabel);
			const TArray<FString> Logged = Capture.Take();
			if (Logged.Num() > 0)
			{
				FDnaExploreFinding& Finding = Out.Findings.AddDefaulted_GetRef();
				Finding.Code = TEXT("DNS5101");
				Finding.Message = FString::Printf(TEXT("%s(%d): the block's own steps logged:\n    %s"), *FileLabel, Block.LineNumber(), *FString::Join(Logged, TEXT("\n    ")));
				Out.Repros.AddDefaulted();
			}
		}
		if (!Out.bStepsOk)
		{
			return;
		}

		// -- what to explore for
		FDnaExploreOptions Options;
		TArray<FString> Steps;
		FString DepthLine;
		TArray<FDnaLineNode*> Lines;
		Block.GetContentChildren(Lines);
		auto AddGoal = [&World, &Options](EDnaGoalKind Kind, const FDnaExprPtr& Condition, const FString& Source, const FString& Where)
		{
			const int32 Index = World.CompileAst(Condition);
			if (Index == INDEX_NONE)
			{
				World.ReportError(FString::Printf(TEXT("%s: `%s` could not be compiled"), *Where, *Source));
				return;
			}
			FDnaExploreGoal& Goal = Options.Goals.AddDefaulted_GetRef();
			Goal.Kind = Kind;
			Goal.Expr = Index;
			Goal.Source = Source;
			Goal.Where = Where;
		};
		for (const FDnaLineNode* Line : Lines)
		{
			if (Line->Kind != EDnaLineKind::TestStep)
			{
				continue;
			}
			const FDnaLinePayload& P = Line->Payload;
			const FString Where = FString::Printf(TEXT("%s(%d)"), *FileLabel, Line->LineNumber());
			switch (P.TestStep)
			{
			case EDnaTestStepKind::Depth:
				Options.MaxDepth = P.Depth;
				DepthLine = Line->Raw.Content;
				break;
			case EDnaTestStepKind::Always:
			case EDnaTestStepKind::Eventually:
				AddGoal(P.TestStep == EDnaTestStepKind::Always ? EDnaGoalKind::Always : EDnaGoalKind::Eventually, P.Condition, AfterKeyword(*Line), Where);
				break;
			case EDnaTestStepKind::Softlock:
				for (const FDnaExprPtr& Goal : P.Goals)
				{
					if (!Goal.IsValid())
					{
						continue;
					}
					// A bare quest name stands for the quest's completion.
					if (Goal->Kind == EDnaExprKind::Name && Goal->Path.Num() == 1 && World.Program().IsQuest(Goal->Path[0]))
					{
						const FDnaExprPtr Completed = MakeShared<FDnaExpr>(*Goal);
						Completed->Name += TEXT(".completed");
						Completed->Path.Add(TEXT("completed"));
						AddGoal(EDnaGoalKind::Softlock, Completed, Completed->Name, Where);
					}
					else
					{
						AddGoal(EDnaGoalKind::Softlock, Goal, Goal->ToString(), Where);
					}
				}
				break;
			default:
				// A step: part of the way to every state, so part of every repro.
				WriteLines(*Line, 1, Steps);
				break;
			}
		}

		// -- the exploration
		FDnaExplorer Explorer(World, Options);
		Explorer.Run();
		Out.Stats = Explorer.GetStats();
		for (const FDnaExploreFinding& Finding : Explorer.GetFindings())
		{
			Out.Findings.Add(Finding);
			Out.Repros.Add(MakeRepro(World, Block, Finding, Steps, DepthLine));
		}
	}

	void ReportExploreResult(FAutomationTestBase& Test, const FDnaExploreBlockResult& Result)
	{
		const FDnaExploreStats& Stats = Result.Stats;
		Test.AddInfo(FString::Printf(TEXT("%d states, %d transitions, %d ways through moves, %d moves deep, %.2f s%s"),
			Stats.States, Stats.Transitions, Stats.Runs, Stats.Depth, Stats.Seconds, Stats.bExhausted ? TEXT(", explored to the end") : TEXT("")));
		for (int32 Index = 0; Index < Result.Findings.Num(); ++Index)
		{
			const FDnaExploreFinding& Finding = Result.Findings[Index];
			FString Text = Finding.Code + TEXT(": ") + Finding.Message;
			if (Result.Repros.IsValidIndex(Index) && !Result.Repros[Index].IsEmpty())
			{
				Text += TEXT("\nTo see it again, add this block to the file:\n\n") + Result.Repros[Index] + TEXT("\n");
			}
			if (Finding.bWarning)
			{
				Test.AddWarning(Text);
			}
			else
			{
				Test.AddError(Text);
			}
		}
	}
}
