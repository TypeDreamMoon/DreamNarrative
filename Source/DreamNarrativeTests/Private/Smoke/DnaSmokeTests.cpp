// Copyright DreamMoon. All Rights Reserved.
//
// The smoke layer: the plugin inside real engine objects instead of the logic layer's stand-ins.
//
//   DreamNarrative.Smoke.InProcess.*   a game instance made in the running process the way a
//                                      standalone game makes one; runs in the editor with the
//                                      Quick preset
//   DreamNarrative.Smoke.Game.*        the game instance of a real -game process, with its map and
//                                      the scripts the Smoke preset built into /Game/Narrative; the
//                                      tests only exist in a game process

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystemComponent.h"
#include "DreamNarrativeDialogueRunner.h"
#include "DreamNarrativeGASBridge.h"
#include "DreamNarrativeLedger.h"
#include "DreamNarrativeRecorder.h"
#include "DreamNarrativeScript.h"
#include "DreamNarrativeStory.h"
#include "Fixtures/DnaGameFixture.h"
#include "Fixtures/DnaTestWorld.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "GameplayTagContainer.h"
#include "Replay/DnaReplay.h"
#include "Smoke/DnaSmokeSaveGame.h"

namespace UE::DreamNarrative::Tests
{
	static const TCHAR* RingSample = TEXT("Tests/Corpus/Validate/Samples/ring.dna");
	static const TCHAR* RpgSample = TEXT("Tests/Corpus/Validate/Samples/rpg.dna");

	/** Types into a game's console and reads the narrative's answer back from the log. */
	class FDnaConsole
	{
	public:
		FDnaConsole(FAutomationTestBase& InTest, const FDnaGameBase& InGame) : Test(InTest), Game(InGame) {}

		/** Everything the narrative logged while the command ran, one line per entry. */
		FString Type(const FString& Command)
		{
			Log.Take();
			if (!Game.Exec(Command))
			{
				Test.AddError(FString::Printf(TEXT("The console did not take `%s`"), *Command));
			}
			return FString::Join(Log.Take(), TEXT("\n"));
		}

		/** Fails the test unless the answer to Command contains Expected. */
		bool Says(const FString& Command, const FString& Expected)
		{
			const FString Answer = Type(Command);
			if (!Answer.Contains(Expected))
			{
				Test.AddError(FString::Printf(TEXT("`%s` should answer \"%s\"; it answered:\n%s"), *Command, *Expected, *Answer));
				return false;
			}
			return true;
		}

	private:
		FAutomationTestBase& Test;
		const FDnaGameBase& Game;
		FDnaNarrativeLog Log;
	};

	/**
	 * The Ring sample's three conversations with 凛, typed into the game's console the way a person
	 * checks them by hand: the first-water entry; at cycle 1 the everyday entry with its once-choice;
	 * the second visit's else branch with the once-choice gone. Which entry and which branch run
	 * follows the facts each step left behind.
	 */
	bool WalkRingThroughConsole(FAutomationTestBase& Test, const FDnaGameBase& Game)
	{
		FDnaConsole Console(Test, Game);

		// Nothing has happened yet, so the first-water entry applies.
		Console.Says(TEXT("dn.talk 凛"), TEXT("凛: 水该换了。"));
		Console.Says(TEXT("dn.next"), TEXT("玩家: 我来。"));
		Console.Says(TEXT("dn.next"), TEXT("凛: 第一滴。"));
		Console.Says(TEXT("dn.next"), TEXT("[dialogue ended]"));
		Console.Says(TEXT("dn.fact 换水次数"), TEXT("换水次数 = 1"));
		Console.Says(TEXT("dn.fact 梅蕾状态"), TEXT("梅蕾状态 = 初绽"));
		Console.Says(TEXT("dn.facts"), TEXT("chapter = 第一章"));

		// The game moves the cycle on. At 1 the everyday entry applies, on its first visit.
		Console.Says(TEXT("dn.fact 周期 1"), TEXT("周期 = 1"));
		Console.Says(TEXT("dn.talk 凛"), TEXT("凛: 你回来了。"));
		Console.Says(TEXT("dn.next"), TEXT("[0] 你在等什么?"));
		Console.Says(TEXT("dn.choose 0"), TEXT("凛: ……"));
		Console.Says(TEXT("dn.next"), TEXT("[dialogue ended]"));
		Console.Says(TEXT("dn.fact 凛提到过等待"), TEXT("凛提到过等待 = true"));

		// The second visit: the else branch, and the once-choice is not offered again.
		Console.Says(TEXT("dn.talk 凛"), TEXT("凛: 嗯。"));
		const FString Choices = Console.Type(TEXT("dn.next"));
		if (!Choices.Contains(TEXT("[0] 没事。")) || Choices.Contains(TEXT("你在等什么")))
		{
			Test.AddError(FString::Printf(TEXT("The second visit should offer only \"没事。\"; it offered:\n%s"), *Choices));
		}
		Console.Says(TEXT("dn.choose 0"), TEXT("[dialogue ended]"));
		return !Test.HasAnyErrors();
	}
}

using namespace UE::DreamNarrative::Tests;

// ---------------------------------------------------------------- in process

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaSmokeSubsystemsTest, "DreamNarrative.Smoke.InProcess.Subsystems", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDnaSmokeSubsystemsTest::RunTest(const FString& Parameters)
{
	FDnaStandaloneGame Game;
	UDreamNarrativeLedger* Ledger = Game.GetLedger();
	UDreamNarrativeStory* Story = Game.GetStory();
	if (!TestNotNull(TEXT("the engine made the ledger"), Ledger) || !TestNotNull(TEXT("the engine made the Story subsystem"), Story))
	{
		return false;
	}
	// Story declares the ledger as its dependency, so the engine initialised the ledger first and
	// handed Story that same instance.
	TestTrue(TEXT("Story uses the game instance's ledger"), Story->GetLedger() == Ledger);
	TestEqual(TEXT("no script yet"), Ledger->GetScripts().Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaSmokeRingConsoleTest, "DreamNarrative.Smoke.InProcess.RingThroughTheConsole", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDnaSmokeRingConsoleTest::RunTest(const FString& Parameters)
{
	FDnaScratchAssets Assets(*this, { RingSample });
	if (!Assets.IsValid())
	{
		return false;
	}
	FDnaStandaloneGame Game;
	if (!TestNotNull(TEXT("the engine made the ledger"), Game.GetLedger()))
	{
		return false;
	}
	const bool bWalked = WalkRingThroughConsole(*this, Game);
	// dn.talk found the scripts the way a game does: through the asset registry, under the asset directory.
	const TArray<TObjectPtr<UDreamNarrativeScript>>& Scripts = Game.GetLedger()->GetScripts();
	TestEqual(TEXT("one script registered"), Scripts.Num(), 1);
	if (Scripts.Num() == 1)
	{
		TestTrue(TEXT("from the asset directory"), Scripts[0]->GetPathName().StartsWith(Assets.GetDirectory()));
	}
	return bWalked;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaSmokeSaveGameTest, "DreamNarrative.Smoke.InProcess.SaveGameRoundTrip", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDnaSmokeSaveGameTest::RunTest(const FString& Parameters)
{
	FDnaScratchAssets Assets(*this, { RingSample });
	if (!Assets.IsValid())
	{
		return false;
	}

	// One session plays the first two conversations and saves the way a game does.
	TArray<uint8> Bytes;
	FDnaLedgerSnapshot Saved;
	{
		FDnaStandaloneGame Session;
		FDnaConsole Console(*this, Session);
		Console.Says(TEXT("dn.talk 凛"), TEXT("凛: 水该换了。"));
		Console.Type(TEXT("dn.next"));
		Console.Type(TEXT("dn.next"));
		Console.Says(TEXT("dn.next"), TEXT("[dialogue ended]"));
		Console.Type(TEXT("dn.fact 周期 1"));
		Console.Says(TEXT("dn.talk 凛"), TEXT("凛: 你回来了。"));
		Console.Type(TEXT("dn.next"));
		Console.Type(TEXT("dn.choose 0"));
		Console.Says(TEXT("dn.next"), TEXT("[dialogue ended]"));

		UDnaSmokeSaveGame* SaveGame = Cast<UDnaSmokeSaveGame>(UGameplayStatics::CreateSaveGameObject(UDnaSmokeSaveGame::StaticClass()));
		if (!TestNotNull(TEXT("save game object"), SaveGame))
		{
			return false;
		}
		Saved = Session.GetLedger()->MakeSnapshot();
		SaveGame->Narrative = Saved;
		TestTrue(TEXT("saved to memory"), UGameplayStatics::SaveGameToMemory(SaveGame, Bytes));
	}

	// A new session -- a fresh game instance -- loads the save and carries on where the first stopped.
	FDnaStandaloneGame Session;
	FDnaConsole Console(*this, Session);
	Console.Says(TEXT("dn.load"), TEXT("Registered 1 script(s)"));
	UDnaSmokeSaveGame* Loaded = Cast<UDnaSmokeSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));
	if (!TestNotNull(TEXT("loaded from memory"), Loaded))
	{
		return false;
	}
	Session.GetLedger()->ApplySnapshot(Loaded->Narrative);
	const TArray<FString> Differences = DiffSnapshots(Saved, Session.GetLedger()->MakeSnapshot());
	if (Differences.Num() > 0)
	{
		AddError(FString::Printf(TEXT("The loaded session differs from the saved one:\n    %s"), *FString::Join(Differences, TEXT("\n    "))));
	}
	// The visit and the once-choice came across: the everyday entry takes its else branch and does
	// not offer the question again.
	Console.Says(TEXT("dn.talk 凛"), TEXT("凛: 嗯。"));
	const FString Choices = Console.Type(TEXT("dn.next"));
	TestFalse(TEXT("the once-choice stays taken after loading"), Choices.Contains(TEXT("你在等什么")));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaSmokeWorldClockTest, "DreamNarrative.Smoke.InProcess.WaitOnTheGameClock", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDnaSmokeWorldClockTest::RunTest(const FString& Parameters)
{
	FDnaScratchAssets Assets(*this, { RpgSample });
	if (!Assets.IsValid())
	{
		return false;
	}
	FDnaStandaloneGame Game;
	FDnaConsole Console(*this, Game);

	// 猎户_日常 shows one line, waits half a second of game time, then shows the next.
	Console.Says(TEXT("dn.start 猎户_日常"), TEXT("猎户: 小心点。"));
	Console.Type(TEXT("dn.next"));
	UDreamNarrativeDialogueRunner* Runner = Game.GetStory()->GetActiveRunner();
	if (!TestNotNull(TEXT("a dialogue is running"), Runner))
	{
		return false;
	}
	TestEqual(TEXT("waiting"), Runner->GetState(), EDnaRunnerState::Waiting);

	// The wait runs on the game's clock: the world's time, through the game instance's timers.
	Game.Tick(0.1f, 3);
	TestEqual(TEXT("still waiting after 0.3s"), Runner->GetState(), EDnaRunnerState::Waiting);
	Game.Tick(0.1f, 5);
	TestEqual(TEXT("the next line after 0.8s"), Runner->GetState(), EDnaRunnerState::ShowingLine);
	TestEqual(TEXT("which is the last one"), Runner->GetCurrentLine().Text.ToString(), FString(TEXT("走吧。")));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaSmokeQuestTimeoutSaveTest, "DreamNarrative.Smoke.InProcess.QuestTimeoutAcrossASave", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDnaSmokeQuestTimeoutSaveTest::RunTest(const FString& Parameters)
{
	FDnaScratchAssets Assets(*this, { TEXT("Tests/Smoke/QuestClock.dna") });
	if (!Assets.IsValid())
	{
		return false;
	}
	const FName Quest(TEXT("送信"));
	const FName Timer = ::DreamNarrative::QuestStateFact(Quest);

	// One session takes the errand on, lets some game time pass, and saves. (The game clock moves at
	// most 0.4 s a frame, and a timer set now starts counting on the next frame.)
	TArray<uint8> Bytes;
	double Left = 0.0;
	{
		FDnaStandaloneGame Session;
		FDnaConsole Console(*this, Session);
		Console.Says(TEXT("dn.accept 送信"), TEXT("送信: Accepted"));
		Session.Tick(0.4f, 3);
		Left = Session.GetLedger()->GetTimerRemaining(Timer);
		TestTrue(FString::Printf(TEXT("game time ran on the quest's clock (%.2f s of 3 left)"), Left), Left > 0.5 && Left < 3.0);

		UDnaSmokeSaveGame* SaveGame = Cast<UDnaSmokeSaveGame>(UGameplayStatics::CreateSaveGameObject(UDnaSmokeSaveGame::StaticClass()));
		if (!TestNotNull(TEXT("save game object"), SaveGame))
		{
			return false;
		}
		SaveGame->Narrative = Session.GetLedger()->MakeSnapshot();
		const float* Saved = SaveGame->Narrative.Timers.Find(Timer);
		TestTrue(TEXT("the save keeps what the quest's clock had left"), Saved && FMath::IsNearlyEqual(*Saved, Left, 0.01));
		TestTrue(TEXT("saved to memory"), UGameplayStatics::SaveGameToMemory(SaveGame, Bytes));
	}

	// A new session loads it: the quest is still taken on, and its clock goes on from what it had left.
	FDnaStandaloneGame Session;
	FDnaConsole Console(*this, Session);
	Console.Says(TEXT("dn.load"), TEXT("Registered 1 script(s)"));
	UDnaSmokeSaveGame* Loaded = Cast<UDnaSmokeSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));
	if (!TestNotNull(TEXT("loaded from memory"), Loaded))
	{
		return false;
	}
	Session.GetLedger()->ApplySnapshot(Loaded->Narrative);
	UDreamNarrativeStory* Story = Session.GetStory();
	TestEqual(TEXT("still taken on after loading"), Story->GetQuestStateOf(Quest), EDnaQuestState::Accepted);
	TestEqual(TEXT("with the time it had left"), Session.GetLedger()->GetTimerRemaining(Timer), Left, 0.01);
	// Short of what was left, it holds; a frame or two past it, it has run out.
	const int32 Frames = FMath::CeilToInt(Left / 0.4);
	Session.Tick(0.4f, Frames - 1);
	TestEqual(TEXT("not out of time before what was left runs out"), Story->GetQuestStateOf(Quest), EDnaQuestState::Accepted);
	Session.Tick(0.4f, 2);
	TestEqual(TEXT("out of time once it has"), Story->GetQuestStateOf(Quest), EDnaQuestState::TimedOut);
	Console.Says(TEXT("dn.quests"), TEXT("信没送到。"));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaSmokeChannelsTest, "DreamNarrative.Smoke.InProcess.ChannelsInTheGame", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDnaSmokeChannelsTest::RunTest(const FString& Parameters)
{
	FDnaScratchAssets Assets(*this, { TEXT("Tests/Smoke/Channels.dna") });
	if (!Assets.IsValid())
	{
		return false;
	}
	FDnaStandaloneGame Game;
	FDnaConsole Console(*this, Game);
	Console.Says(TEXT("dn.load"), TEXT("Registered 1 script(s)"));
	UDreamNarrativeStory* Story = Game.GetStory();
	UDreamNarrativeGASBridge* Bridge = Game.GetGameInstance()->GetSubsystem<UDreamNarrativeGASBridge>();
	if (!TestNotNull(TEXT("the GAS bridge is a subsystem of the game instance"), Bridge))
	{
		return false;
	}

	// The boss: an actor in the world with an ability system, which the game hands to the bridge.
	AActor* Boss = Game.GetWorld()->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("the boss"), Boss))
	{
		return false;
	}
	UAbilitySystemComponent* System = NewObject<UAbilitySystemComponent>(Boss);
	System->RegisterComponent();
	System->InitAbilityActorInfo(Boss, Boss);
	Bridge->WatchAbilitySystem(System, /*bMirrorTags=*/false);

	// The chatter starts and the boss changes phase meanwhile: two channels talk at once, and the
	// player keeps the controls.
	Console.Says(TEXT("dn.emit 闲置"), TEXT("你觉得机械生命体会做梦吗?"));
	// The tag is one the test host registers (Tools/TestHost/Template/Config/Tags).
	const FGameplayTag PhaseTag = FGameplayTag::RequestGameplayTag(TEXT("DreamNarrativeTest.Boss.Phase"), /*ErrorIfNotFound=*/false);
	if (!TestTrue(TEXT("the test host registers the boss's phase tag"), PhaseTag.IsValid()))
	{
		return false;
	}
	FGameplayEventData Phase;
	Phase.EventMagnitude = 2.f;
	System->HandleGameplayEvent(PhaseTag, &Phase);
	UDreamNarrativeDialogueRunner* Chatter = Story->GetRunner(TEXT("闲聊"));
	UDreamNarrativeDialogueRunner* Report = Story->GetRunner(TEXT("侦察"));
	if (!TestNotNull(TEXT("the chatter talks"), Chatter) || !TestNotNull(TEXT("the report came through the bridge"), Report))
	{
		return false;
	}
	TestEqual(TEXT("the report's first line"), Report->GetCurrentLine().Text.ToString(), FString(TEXT("目标装甲脱落。")));
	TestFalse(TEXT("neither takes the controls"), Story->IsInputLocked());

	// The report moves on by itself on the world's time: its first line lasts 1.5 s.
	Game.Tick(0.4f, 6);
	TestEqual(TEXT("the report's next line, by itself"), Report->GetCurrentLine().Text.ToString(), FString(TEXT("建议:集中攻击核心。")));

	// Talking to 9S holds the chatter still -- not the report -- until the talk is over.
	const FString Held = Chatter->GetCurrentLine().Text.ToString();
	Console.Says(TEXT("dn.talk 九S"), TEXT("先别说话,跟上。"));
	TestTrue(TEXT("the talk takes the controls"), Story->IsInputLocked());
	TestTrue(TEXT("the chatter holds still"), Chatter->IsPaused());
	Game.Tick(0.4f, 10);
	TestEqual(TEXT("time passing does not move it"), Chatter->GetCurrentLine().Text.ToString(), Held);
	TestNull(TEXT("the report finished meanwhile"), Story->GetRunner(TEXT("侦察")));
	Console.Says(TEXT("dn.next"), TEXT("[dialogue ended]"));
	TestFalse(TEXT("the chatter goes on"), Chatter->IsPaused());
	TestEqual(TEXT("from the line it was held at"), Chatter->GetCurrentLine().Text.ToString(), Held);
	Game.Tick(0.4f, 15);
	TestNull(TEXT("and says the rest by itself"), Story->GetRunner(TEXT("闲聊")));

	Bridge->UnwatchAbilitySystem(System);
	Boss->Destroy();
	return !HasAnyErrors();
}

// ---------------------------------------------------------------- in a game process

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaSmokeGameRingTest, "DreamNarrative.Smoke.Game.RingInTheGame", EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FDnaSmokeGameRingTest::RunTest(const FString& Parameters)
{
	FDnaGameInProcess Game;
	if (!Game.GetWorld())
	{
		AddError(TEXT("No game world in this process; the Smoke.Game tests run in a -game process (dnc test -Preset Smoke)"));
		return false;
	}
	UDreamNarrativeLedger* Ledger = Game.GetLedger();
	if (!TestNotNull(TEXT("the game instance has a ledger"), Ledger) || !TestNotNull(TEXT("and a Story subsystem"), Game.GetStory()))
	{
		return false;
	}
	// The walk starts from a game nobody has played; a second run in the same process starts over.
	if (Ledger->GetScripts().Num() > 0)
	{
		Ledger->ResetToDefaults();
	}
	// Recorded while it is walked, the way a tester records a session.
	FDnaConsole Console(*this, Game);
	const FString RecordingPath = UDreamNarrativeRecorder::GetDefaultPath(TEXT("冒烟回放"));
	Console.Says(TEXT("dn.record start 冒烟回放"), TEXT("Recording 冒烟回放"));
	const bool bWalked = WalkRingThroughConsole(*this, Game);
	Console.Says(TEXT("dn.record stop"), TEXT("Wrote "));

	// The scripts are the ones the Smoke preset built before the game started, found on disk.
	bool bFromDisk = false;
	for (const UDreamNarrativeScript* Script : Ledger->GetScripts())
	{
		bFromDisk |= Script && Script->GetPathName() == TEXT("/Game/Narrative/ring.ring");
	}
	TestTrue(TEXT("ring was loaded from /Game/Narrative"), bFromDisk);

	// The recording plays back in the logic layer. It is checked here, not kept: the project is left
	// as it was, down to the directories the recording made.
	FString Recorded;
	const bool bWritten = FFileHelper::LoadFileToString(Recorded, *RecordingPath);
	TestTrue(TEXT("dn.record stop wrote the recording"), bWritten);
	IFileManager::Get().Delete(*RecordingPath, /*RequireExists=*/false, /*EvenReadOnly=*/true, /*Quiet=*/true);
	FString Directory = FPaths::GetPath(RecordingPath);
	for (int32 Level = 0; Level < 3; ++Level, Directory = FPaths::GetPath(Directory))
	{
		IFileManager::Get().DeleteDirectory(*Directory, /*RequireExists=*/false, /*Tree=*/false);
	}
	TArray<FDnaTestSource> Sources;
	if (bWritten && ReadPluginSources(*this, { RingSample }, Sources))
	{
		Sources.Add({ TEXT("冒烟回放.dna"), Recorded });
		FDnaTestWorld World(*this);
		if (World.Compile(Sources))
		{
			TestEqual(TEXT("the recording plays back"), FString::Join(Replay::RunCollecting(World, TEXT("冒烟回放"), TEXT("冒烟回放.dna")), TEXT("\n")), FString());
		}
	}
	return bWalked && !HasAnyErrors();
}

#endif
