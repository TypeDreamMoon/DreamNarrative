// Copyright DreamMoon. All Rights Reserved.
//
// The `dn.` console commands: enough to walk a dialogue in PIE without any UI. Lines and choices go
// to the log and to the screen as debug messages; the game's real UI replaces all of this.

#include "DreamNarrativeDialogueRunner.h"
#include "DreamNarrativeLedger.h"
#include "DreamNarrativeStory.h"
#include "DreamNarrativeStoryModule.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

namespace
{
	void Say(const FString& Message, FColor Color = FColor::White)
	{
		UE_LOG(LogDreamNarrativeStory, Display, TEXT("%s"), *Message);
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 8.f, Color, Message);
		}
	}

	UDreamNarrativeStory* StoryOf(UWorld* World)
	{
		UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
		// A command typed into the editor's own console arrives with the editor world, which has no
		// game instance. Fall back to whatever play world is running so `dn.` works from there too.
		if (!GameInstance && GEngine)
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if ((Context.WorldType == EWorldType::PIE || Context.WorldType == EWorldType::Game) && Context.World() && Context.World()->GetGameInstance())
				{
					GameInstance = Context.World()->GetGameInstance();
					break;
				}
			}
		}
		return GameInstance ? GameInstance->GetSubsystem<UDreamNarrativeStory>() : nullptr;
	}

	void PrintRunner(UDreamNarrativeDialogueRunner* Runner)
	{
		if (!Runner)
		{
			return;
		}
		switch (Runner->GetState())
		{
		case EDnaRunnerState::ShowingLine:
		{
			const FDnaLineView& Line = Runner->GetCurrentLine();
			Say(Line.bNarration
				? FString::Printf(TEXT("  %s"), *Line.Text.ToString())
				: FString::Printf(TEXT("%s: %s"), *Line.SpeakerDisplay.ToString(), *Line.Text.ToString()), FColor::Yellow);
			Say(TEXT("    (dn.next)"), FColor::Silver);
			break;
		}
		case EDnaRunnerState::AwaitingChoice:
			for (const FDnaChoiceView& Choice : Runner->GetChoices())
			{
				Say(FString::Printf(TEXT("  [%d] %s"), Choice.Index, *Choice.Label.ToString()), FColor::Cyan);
			}
			Say(TEXT("    (dn.choose <n>)"), FColor::Silver);
			break;
		case EDnaRunnerState::Waiting:
			Say(TEXT("  ..."), FColor::Silver);
			break;
		case EDnaRunnerState::PlayingStage:
			Say(TEXT("  [playing a stage; dn.next continues]"), FColor::Silver);
			break;
		case EDnaRunnerState::Ended:
			Say(TEXT("  [dialogue ended]"), FColor::Silver);
			break;
		default:
			break;
		}
	}

	FAutoConsoleCommandWithWorldAndArgs GTalk(
		TEXT("dn.talk"), TEXT("dn.talk <character> -- start the entry dialogue of a character"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UDreamNarrativeStory* Story = StoryOf(World);
			if (!Story || Args.Num() < 1) { Say(TEXT("dn.talk <character>")); return; }
			if (Story->GetLedger() && Story->GetLedger()->GetScripts().Num() == 0) { Story->LoadAllScripts(); }
			UDreamNarrativeDialogueRunner* Runner = Story->TalkTo(FName(*Args[0]));
			if (!Runner) { Say(FString::Printf(TEXT("No entry of '%s' applies"), *Args[0]), FColor::Red); return; }
			PrintRunner(Runner);
		}));

	FAutoConsoleCommandWithWorldAndArgs GStart(
		TEXT("dn.start"), TEXT("dn.start <dialogue> -- start a dialogue node by name"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UDreamNarrativeStory* Story = StoryOf(World);
			if (!Story || Args.Num() < 1) { Say(TEXT("dn.start <dialogue>")); return; }
			if (Story->GetLedger() && Story->GetLedger()->GetScripts().Num() == 0) { Story->LoadAllScripts(); }
			PrintRunner(Story->StartDialogue(FName(*Args[0])));
		}));

	FAutoConsoleCommandWithWorldAndArgs GNext(
		TEXT("dn.next"), TEXT("dn.next -- advance past the current line"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
		{
			UDreamNarrativeStory* Story = StoryOf(World);
			UDreamNarrativeDialogueRunner* Runner = Story ? Story->GetActiveRunner() : nullptr;
			if (!Runner) { Say(TEXT("No dialogue is running")); return; }
			if (Runner->GetState() == EDnaRunnerState::PlayingStage) { Runner->NotifyStageFinished(); }
			else { Runner->Advance(); }
			PrintRunner(Runner);
		}));

	FAutoConsoleCommandWithWorldAndArgs GChoose(
		TEXT("dn.choose"), TEXT("dn.choose <n> -- pick a choice"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UDreamNarrativeStory* Story = StoryOf(World);
			UDreamNarrativeDialogueRunner* Runner = Story ? Story->GetActiveRunner() : nullptr;
			if (!Runner || Args.Num() < 1) { Say(TEXT("dn.choose <n>")); return; }
			if (!Runner->Choose(FCString::Atoi(*Args[0]))) { Say(TEXT("Not a valid choice right now"), FColor::Red); }
			PrintRunner(Runner);
		}));

	FAutoConsoleCommandWithWorldAndArgs GFact(
		TEXT("dn.fact"), TEXT("dn.fact <name> [value] -- read or write a fact (as game code)"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UDreamNarrativeStory* Story = StoryOf(World);
			UDreamNarrativeLedger* Ledger = Story ? Story->GetLedger() : nullptr;
			if (!Ledger || Args.Num() < 1) { Say(TEXT("dn.fact <name> [value]")); return; }
			const FName Name(*Args[0]);
			const FDnaFactDecl* Decl = Ledger->FindFactDecl(Name);
			if (!Decl) { Say(FString::Printf(TEXT("No fact '%s'"), *Args[0]), FColor::Red); return; }
			if (Args.Num() >= 2)
			{
				FDnaValue Value;
				switch (Decl->Type)
				{
				case EDnaFactType::Bool: Value = FDnaValue::FromBool(Args[1] == TEXT("true") || Args[1] == TEXT("1")); break;
				case EDnaFactType::Int: Value = FDnaValue::FromInt(FCString::Atoi(*Args[1])); break;
				case EDnaFactType::Float: Value = FDnaValue::FromFloat(FCString::Atof(*Args[1])); break;
				case EDnaFactType::Tag: Value = FDnaValue::FromTag(FName(*Args[1])); break;
				}
				Ledger->SetFact(Name, Value, EDnaWriteSource::Game);
			}
			Say(FString::Printf(TEXT("%s = %s"), *Args[0], *Ledger->GetFact(Name).ToString()), FColor::Green);
		}));

	FAutoConsoleCommandWithWorldAndArgs GFacts(
		TEXT("dn.facts"), TEXT("dn.facts -- print every fact"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
		{
			UDreamNarrativeStory* Story = StoryOf(World);
			UDreamNarrativeLedger* Ledger = Story ? Story->GetLedger() : nullptr;
			if (!Ledger) { return; }
			TArray<FName> Names;
			Ledger->GetFactNames(Names);
			Names.Sort(FNameLexicalLess());
			Say(FString::Printf(TEXT("chapter = %s"), *Ledger->GetChapter().ToString()), FColor::Green);
			for (const FName& Name : Names)
			{
				Say(FString::Printf(TEXT("%s = %s"), *Name.ToString(), *Ledger->GetFact(Name).ToString()), FColor::Green);
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs GJump(
		TEXT("dn.jump"), TEXT("dn.jump <chapter> -- teleport to a chapter, applying its implied facts"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UDreamNarrativeStory* Story = StoryOf(World);
			UDreamNarrativeLedger* Ledger = Story ? Story->GetLedger() : nullptr;
			if (!Ledger || Args.Num() < 1) { Say(TEXT("dn.jump <chapter>")); return; }
			if (Ledger->JumpToChapter(FName(*Args[0]))) { Say(FString::Printf(TEXT("chapter = %s"), *Ledger->GetChapter().ToString()), FColor::Green); }
			else { Say(FString::Printf(TEXT("No chapter '%s'"), *Args[0]), FColor::Red); }
		}));

	FAutoConsoleCommandWithWorldAndArgs GLoad(
		TEXT("dn.load"), TEXT("dn.load -- register every narrative script asset"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
		{
			UDreamNarrativeStory* Story = StoryOf(World);
			if (!Story) { return; }
			Say(FString::Printf(TEXT("Registered %d script(s)"), Story->LoadAllScripts()), FColor::Green);
		}));
}
