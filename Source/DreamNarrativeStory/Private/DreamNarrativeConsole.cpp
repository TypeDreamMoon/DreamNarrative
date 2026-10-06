// Copyright DreamMoon. All Rights Reserved.
//
// The `dn.` console commands: enough to walk a dialogue in PIE without any UI. Lines and choices go
// to the log and to the screen as debug messages; the game's real UI replaces all of this.
// `dn.record` writes whatever is done to the story, through these commands or the game's own UI,
// down as a test that plays it back.

#include "DreamNarrativeDialogueRunner.h"
#include "DreamNarrativeLedger.h"
#include "DreamNarrativeRecorder.h"
#include "DreamNarrativeStory.h"
#include "DreamNarrativeStoryModule.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

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

	/** What each channel that is not quiet is doing. */
	void PrintChannels(const UDreamNarrativeStory& Story)
	{
		TArray<FName> Channels;
		Story.GetChannelNames(Channels);
		bool bAny = false;
		for (const FName& Channel : Channels)
		{
			const UDreamNarrativeDialogueRunner* Runner = Story.GetRunner(Channel);
			const int32 Waiting = Story.GetQueueLength(Channel);
			if (!Runner && Waiting == 0)
			{
				continue;
			}
			bAny = true;
			FString Doing = TEXT("quiet");
			if (Runner)
			{
				Doing = Runner->GetNodeName().ToString();
				if (Runner->IsPaused())
				{
					Doing += TEXT(", paused");
				}
				else if (Runner->GetState() == EDnaRunnerState::ShowingLine)
				{
					const FDnaLineView& Line = Runner->GetCurrentLine();
					Doing += TEXT(": ") + (Line.bNarration ? Line.Text.ToString() : FString::Printf(TEXT("%s: %s"), *Line.SpeakerDisplay.ToString(), *Line.Text.ToString()));
				}
				else if (Runner->GetState() == EDnaRunnerState::AwaitingChoice)
				{
					Doing += TEXT(", choosing");
				}
			}
			Say(FString::Printf(TEXT("[%s] %s%s"), *Channel.ToString(), *Doing, Waiting > 0 ? *FString::Printf(TEXT(" (+%d waiting)"), Waiting) : TEXT("")), FColor::Cyan);
		}
		if (!bAny)
		{
			Say(TEXT("Every channel is quiet"));
		}
	}

	/** A value typed at the console, as a fact or parameter of this type. */
	FDnaValue ParseValue(EDnaFactType Type, const FString& Text)
	{
		switch (Type)
		{
		case EDnaFactType::Bool: return FDnaValue::FromBool(Text == TEXT("true") || Text == TEXT("1"));
		case EDnaFactType::Int: return FDnaValue::FromInt(FCString::Atoi(*Text));
		case EDnaFactType::Float: return FDnaValue::FromFloat(FCString::Atof(*Text));
		case EDnaFactType::Tag: return FDnaValue::FromTag(FName(*Text));
		}
		return FDnaValue();
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
		TEXT("dn.next"), TEXT("dn.next [channel] -- advance past the current line (of main, or of the channel)"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UDreamNarrativeStory* Story = StoryOf(World);
			UDreamNarrativeDialogueRunner* Runner = Story ? Story->GetRunner(Args.Num() > 0 ? FName(*Args[0]) : NAME_None) : nullptr;
			if (!Runner) { Say(TEXT("No dialogue is running")); return; }
			if (Runner->GetState() == EDnaRunnerState::PlayingStage) { Runner->NotifyStageFinished(); }
			else { Runner->Advance(); }
			PrintRunner(Runner);
		}));

	FAutoConsoleCommandWithWorldAndArgs GChoose(
		TEXT("dn.choose"), TEXT("dn.choose <n> [channel] -- pick a choice (on main, or on the channel)"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UDreamNarrativeStory* Story = StoryOf(World);
			UDreamNarrativeDialogueRunner* Runner = Story ? Story->GetRunner(Args.Num() > 1 ? FName(*Args[1]) : NAME_None) : nullptr;
			if (!Runner || Args.Num() < 1) { Say(TEXT("dn.choose <n> [channel]")); return; }
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
				Ledger->SetFact(Name, ParseValue(Decl->Type, Args[1]), EDnaWriteSource::Game);
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

	FAutoConsoleCommandWithWorldAndArgs GEmit(
		TEXT("dn.emit"), TEXT("dn.emit <event> [param=value ...] -- raise an event (as the game), then show what each channel says"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UDreamNarrativeStory* Story = StoryOf(World);
			UDreamNarrativeLedger* Ledger = Story ? Story->GetLedger() : nullptr;
			if (!Ledger || Args.Num() < 1) { Say(TEXT("dn.emit <event> [param=value ...]")); return; }
			if (Ledger->GetScripts().Num() == 0) { Story->LoadAllScripts(); }
			FDnaEventPayload Event;
			Event.Event = FName(*Args[0]);
			const FDnaEventDecl* Decl = Ledger->FindEventDecl(Event.Event);
			for (int32 Index = 1; Index < Args.Num(); ++Index)
			{
				FString Param;
				FString Text;
				const FDnaFactDecl* ParamDecl = Decl && Args[Index].Split(TEXT("="), &Param, &Text) ? Decl->FindParam(FName(*Param.TrimStartAndEnd())) : nullptr;
				if (!ParamDecl) { Say(FString::Printf(TEXT("'%s' is not a parameter of %s; write param=value"), *Args[Index], *Args[0]), FColor::Red); return; }
				Event.Set(ParamDecl->Name, ParseValue(ParamDecl->Type, Text.TrimStartAndEnd()));
			}
			Ledger->RaiseEvent(Event);
			Say(FString::Printf(TEXT("Raised %s"), *Event.ToString()), FColor::Green);
			PrintChannels(*Story);
		}));

	FAutoConsoleCommandWithWorldAndArgs GChannels(
		TEXT("dn.channels"), TEXT("dn.channels -- what each channel is saying, and how many dialogues wait their turn"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
		{
			if (const UDreamNarrativeStory* Story = StoryOf(World))
			{
				PrintChannels(*Story);
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs GNewGame(
		TEXT("dn.newgame"), TEXT("dn.newgame -- start a new playthrough: the save slot back to defaults, the profile kept, the first chapter entered"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
		{
			UDreamNarrativeStory* Story = StoryOf(World);
			UDreamNarrativeLedger* Ledger = Story ? Story->GetLedger() : nullptr;
			if (!Ledger) { return; }
			if (Ledger->GetScripts().Num() == 0) { Story->LoadAllScripts(); }
			Ledger->StartNewPlaythrough();
			Say(FString::Printf(TEXT("New playthrough; chapter = %s"), *Ledger->GetChapter().ToString()), FColor::Green);
		}));

	FAutoConsoleCommandWithWorldAndArgs GSelect(
		TEXT("dn.select"), TEXT("dn.select <chapter> -- go back to the checkpoint of a chapter the story has entered, keeping progress"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UDreamNarrativeStory* Story = StoryOf(World);
			UDreamNarrativeLedger* Ledger = Story ? Story->GetLedger() : nullptr;
			if (!Ledger || Args.Num() < 1) { Say(TEXT("dn.select <chapter>")); return; }
			if (Ledger->SelectChapter(FName(*Args[0]))) { Say(FString::Printf(TEXT("chapter = %s"), *Ledger->GetChapter().ToString()), FColor::Green); }
			else { Say(FString::Printf(TEXT("No checkpoint for '%s'"), *Args[0]), FColor::Red); }
		}));

	FAutoConsoleCommandWithWorldAndArgs GCheckpoints(
		TEXT("dn.checkpoints"), TEXT("dn.checkpoints -- the chapters that can be selected, in the order the story entered them"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
		{
			UDreamNarrativeStory* Story = StoryOf(World);
			UDreamNarrativeLedger* Ledger = Story ? Story->GetLedger() : nullptr;
			if (!Ledger) { return; }
			TArray<FString> Chapters;
			for (const FName& Chapter : Ledger->GetChapterCheckpoints()) { Chapters.Add(Chapter.ToString()); }
			Say(Chapters.Num() > 0 ? FString::Join(Chapters, TEXT(", ")) : FString(TEXT("No checkpoints yet")), FColor::Green);
		}));

	FAutoConsoleCommandWithWorldAndArgs GArchive(
		TEXT("dn.archive"), TEXT("dn.archive [name] -- an archive's unlocked entries; alone, every archive with how many are unlocked"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UDreamNarrativeStory* Story = StoryOf(World);
			if (!Story) { return; }
			TArray<FName> Names;
			if (Args.Num() > 0) { Names.Add(FName(*Args[0])); }
			else { Story->GetArchiveNames(Names); }
			for (const FName& Name : Names)
			{
				const FDnaArchiveView View = Story->GetArchive(Name);
				if (View.Name.IsNone()) { Say(FString::Printf(TEXT("No archive '%s'"), *Name.ToString()), FColor::Red); continue; }
				Say(FString::Printf(TEXT("%s (%d/%d)"), *View.Title.ToString(), View.Entries.Num(), View.Total), FColor::Green);
				if (Args.Num() > 0)
				{
					for (const FText& Entry : View.Entries) { Say(TEXT("  ") + Entry.ToString(), FColor::Silver); }
				}
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

	void QuestCommand(const TArray<FString>& Args, UWorld* World, EDnaQuestRequest What, const TCHAR* Usage)
	{
		UDreamNarrativeStory* Story = StoryOf(World);
		if (!Story || Args.Num() < 1) { Say(Usage); return; }
		if (Story->GetLedger() && Story->GetLedger()->GetScripts().Num() == 0) { Story->LoadAllScripts(); }
		const FName Quest(*Args[0]);
		// Asked as the game: this is what a game's own code does.
		Story->RequestQuest(What, Quest, EDnaWriteSource::Game);
		Say(FString::Printf(TEXT("%s: %s"), *Args[0], *::DreamNarrative::QuestStateMember(Story->GetQuestStateOf(Quest)).ToString()), FColor::Green);
	}

	FAutoConsoleCommandWithWorldAndArgs GAccept(
		TEXT("dn.accept"), TEXT("dn.accept <quest> -- take a quest on, as the game"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			QuestCommand(Args, World, EDnaQuestRequest::Accept, TEXT("dn.accept <quest>"));
		}));

	FAutoConsoleCommandWithWorldAndArgs GComplete(
		TEXT("dn.complete"), TEXT("dn.complete <quest> -- complete an active quest, as the game"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			QuestCommand(Args, World, EDnaQuestRequest::Complete, TEXT("dn.complete <quest>"));
		}));

	FAutoConsoleCommandWithWorldAndArgs GFail(
		TEXT("dn.fail"), TEXT("dn.fail <quest> -- fail an active quest, as the game"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			QuestCommand(Args, World, EDnaQuestRequest::Fail, TEXT("dn.fail <quest>"));
		}));

	FAutoConsoleCommandWithWorldAndArgs GQuests(
		TEXT("dn.quests"), TEXT("dn.quests -- every quest: its state, journal line, objectives and time left"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
		{
			UDreamNarrativeStory* Story = StoryOf(World);
			UDreamNarrativeLedger* Ledger = Story ? Story->GetLedger() : nullptr;
			if (!Ledger) { return; }
			TArray<FName> Quests;
			Story->GetQuestNames(Quests);
			Quests.Sort(FNameLexicalLess());
			for (const FName& Quest : Quests)
			{
				const double Left = Ledger->GetTimerRemaining(::DreamNarrative::QuestStateFact(Quest));
				Say(FString::Printf(TEXT("%s: %s%s  %s"), *Quest.ToString(), *::DreamNarrative::QuestStateMember(Story->GetQuestStateOf(Quest)).ToString(),
					Left >= 0.0 ? *FString::Printf(TEXT(" (%.0fs left)"), Left) : TEXT(""), *Story->GetQuestJournal(Quest).ToString()), FColor::Green);
				for (const FDnaObjectiveView& Objective : Story->GetQuestObjectives(Quest))
				{
					Say(FString::Printf(TEXT("    [%s] %s%s"), Objective.bDone ? TEXT("x") : TEXT(" "), *Objective.Text.ToString(), Objective.bOptional ? TEXT(" (optional)") : TEXT("")), FColor::Silver);
				}
			}
		}));

	/** A recording's name is its test's name: letters, digits, underscores and CJK, not starting with a digit. */
	FString MakeTestName(const FString& Wanted)
	{
		FString Name;
		for (const TCHAR Char : Wanted)
		{
			Name.AppendChar(FChar::IsAlnum(Char) || Char == TEXT('_') || Char > 0x7F ? Char : TEXT('_'));
		}
		return Name.IsEmpty() || FChar::IsDigit(Name[0]) ? TEXT("录制_") + Name : Name;
	}

	FAutoConsoleCommandWithWorldAndArgs GRecord(
		TEXT("dn.record"), TEXT("dn.record start <name> | stop -- write what happens to the story down as a test; alone, says whether it is recording"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UDreamNarrativeStory* Story = StoryOf(World);
			if (!Story) { Say(TEXT("No story is running"), FColor::Red); return; }
			UDreamNarrativeRecorder* Recorder = Story->GetRecorder();
			const bool bRecording = Recorder && Recorder->IsRecording();
			if (Args.Num() >= 2 && Args[0] == TEXT("start"))
			{
				if (Story->GetLedger() && Story->GetLedger()->GetScripts().Num() == 0) { Story->LoadAllScripts(); }
				const FString Name = MakeTestName(Args[1]);
				Story->StartRecording(Name);
				Say(FString::Printf(TEXT("Recording %s; dn.record stop writes %s"), *Name, *UDreamNarrativeRecorder::GetDefaultPath(Name)), FColor::Green);
				return;
			}
			if (Args.Num() >= 1 && Args[0] == TEXT("stop"))
			{
				if (!bRecording) { Say(TEXT("Nothing is being recorded"), FColor::Red); return; }
				const FString Path = UDreamNarrativeRecorder::GetDefaultPath(Recorder->GetName());
				const FString Text = Story->StopRecording();
				IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), /*Tree=*/true);
				if (FFileHelper::SaveStringToFile(Text, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
				{
					Say(FString::Printf(TEXT("Wrote %s"), *Path), FColor::Green);
				}
				else
				{
					Say(FString::Printf(TEXT("Could not write %s"), *Path), FColor::Red);
				}
				return;
			}
			Say(bRecording ? FString::Printf(TEXT("Recording %s"), *Recorder->GetName()) : FString(TEXT("Not recording. dn.record start <name>")));
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
