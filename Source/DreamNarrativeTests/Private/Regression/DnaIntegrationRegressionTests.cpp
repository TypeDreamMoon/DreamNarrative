// Copyright DreamMoon. All Rights Reserved.
#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilitySystemComponent.h"
#include "DreamNarrativeDialogueRunner.h"
#include "DreamNarrativeGASBridge.h"
#include "DreamNarrativeLedger.h"
#include "DreamNarrativeScript.h"
#include "DreamNarrativeStageDirector.h"
#include "DreamNarrativeStageSubsystem.h"
#include "DreamNarrativeStory.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Fixtures/DnaGameFixture.h"
#include "Fixtures/DnaTestWorld.h"
#include "GameFramework/Actor.h"
#include "Lang/DnaEditSession.h"
#include "Lang/DnaStaging.h"

using namespace UE::DreamNarrative::Tests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaRegressionChapterObservers, "DreamNarrative.Regression.Integration.ChapterObservers", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDnaRegressionChapterObservers::RunTest(const FString& Parameters)
{
	FDnaTestWorld World(*this);
	if (!World.CompileText(TEXT(
		"chapter First\nchapter Second after First\n"
		"quest ChapterQuest\n  priority: hidden\n  available if chapter == Second\n"
		"archive ChapterArchive\n  entry if chapter == Second\n    Second chapter reached.\n"))) { return false; }
	World.Ledger().SetChapter(TEXT("First"));
	TestEqual(TEXT("quest initially inactive"), World.Story().GetQuestStateOf(TEXT("ChapterQuest")), EDnaQuestState::Inactive);
	World.Ledger().SetChapter(TEXT("Second"));
	TestEqual(TEXT("chapter change starts eligible hidden quest"), World.Story().GetQuestStateOf(TEXT("ChapterQuest")), EDnaQuestState::Accepted);
	TestTrue(TEXT("chapter change unlocks eligible archive"), World.Story().IsArchiveEntryUnlocked(TEXT("ChapterArchive"), 0));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaRegressionVisitObservers, "DreamNarrative.Regression.Integration.VisitObservers", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDnaRegressionVisitObservers::RunTest(const FString& Parameters)
{
	FDnaTestWorld World(*this);
	if (!World.CompileText(TEXT(
		"character Speaker\ndialogue Intro for Speaker\n  Speaker: Hello.\n"
		"quest TalkQuest\n  objective visited(Intro) >= 1\n"))) { return false; }
	TestTrue(TEXT("quest accepted"), World.Story().AcceptQuest(TEXT("TalkQuest")));
	if (!World.Talk(TEXT("Speaker"))) { return false; }
	TestEqual(TEXT("dialogue has been visited"), World.Ledger().GetVisited(TEXT("Intro")), 1);
	TestEqual(TEXT("visit objective completes without unrelated fact writes"), World.Story().GetQuestStateOf(TEXT("TalkQuest")), EDnaQuestState::Completed);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaRegressionVisitAdvancesChapter, "DreamNarrative.Regression.Integration.VisitAdvancesChapter", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDnaRegressionVisitAdvancesChapter::RunTest(const FString& Parameters)
{
	FDnaTestWorld World(*this);
	if (!World.CompileText(TEXT(
		"character Speaker\ndialogue Finale for Speaker\n  Speaker: Goodbye.\n"
		"chapter Last\n  advance -> Epilogue if visited(Finale) >= 1\n"
		"chapter Epilogue after Last\n"))) { return false; }
	World.Ledger().SetChapter(TEXT("Last"));
	if (!World.Talk(TEXT("Speaker"))) { return false; }
	TestEqual(TEXT("a visit alone moves the chapter on"), World.Ledger().GetChapter(), FName(TEXT("Epilogue")));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaRegressionMultiFileInitialization, "DreamNarrative.Regression.Integration.CrossFileInitialization", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDnaRegressionMultiFileInitialization::RunTest(const FString& Parameters)
{
	const FDnaTestSource Quest = { TEXT("A_Quest.dna"), TEXT("quest HiddenQuest\n  priority: hidden\n  available if not GateClosed\n") };
	const FDnaTestSource Fact = { TEXT("B_Facts.dna"), TEXT("fact GateClosed: bool extern = true\n") };
	FDnaTestWorld Forward(*this), Reverse(*this);
	if (!Forward.Compile({ Quest, Fact }) || !Reverse.Compile({ Fact, Quest })) { return false; }
	TestTrue(TEXT("forward fact default"), Forward.Ledger().GetFactAsBool(TEXT("GateClosed")));
	TestEqual(TEXT("fact-first leaves quest inactive"), Reverse.Story().GetQuestStateOf(TEXT("HiddenQuest")), EDnaQuestState::Inactive);
	TestEqual(TEXT("quest-first must also leave quest inactive"), Forward.Story().GetQuestStateOf(TEXT("HiddenQuest")), EDnaQuestState::Inactive);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaRegressionReentrantStages, "DreamNarrative.Regression.Integration.ReentrantStageReplacement", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDnaRegressionReentrantStages::RunTest(const FString& Parameters)
{
	FDnaTestWorld Definitions(*this);
	if (!Definitions.CompileText(TEXT("stage A\n  duration 1.0s\nstage B\n  duration 1.0s\nstage C\n  duration 1.0s\n"))) { return false; }
	FDnaStandaloneGame Game;
	for (UDreamNarrativeScript* Script : Definitions.GetScripts()) { Game.GetStory()->RegisterScript(Script); }
	UDreamNarrativeStageSubsystem* Host = Game.GetGameInstance()->GetSubsystem<UDreamNarrativeStageSubsystem>();
	UDreamNarrativeStageDirector* Director = UDreamNarrativeStageDirector::FindOrAddDirector(Game.GetWorld()->SpawnActor<AActor>());
	Director->bManualAdvance = true;
	Host->RegisterDirector(Director);
	uint64 ChainedTicket = 0;
	int32 ChainedEnds = 0;
	FDnaStageRequest A, B, C;
	A.Stage = TEXT("A"); B.Stage = TEXT("B"); C.Stage = TEXT("C");
	const uint64 First = Host->PlayStage(A, FDnaStageFinishedDelegate::CreateLambda([&](EDnaStageEnd)
	{
		ChainedTicket = Host->PlayStage(C, FDnaStageFinishedDelegate::CreateLambda([&](EDnaStageEnd) { ++ChainedEnds; }));
	}));
	TestTrue(TEXT("first stage started"), First != 0);
	const uint64 Replacement = Host->PlayStage(B, FDnaStageFinishedDelegate());
	TestEqual(TEXT("outer replacement is superseded by the callback request"), Replacement, uint64(0));
	TestEqual(TEXT("newest request keeps ownership"), Director->GetStageName(), FName(TEXT("C")));
	TestTrue(TEXT("end callback started the chained stage"), ChainedTicket != 0);
	AddInfo(FString::Printf(TEXT("Director after replacement: %s"), *Director->GetStageName().ToString()));
	Director->Advance(3.0f);
	TestFalse(TEXT("chained stage must not leave an orphaned ticket"), Host->IsStagePlaying(ChainedTicket));
	TestEqual(TEXT("chained stage receives exactly one end"), ChainedEnds, 1);
	Host->StopAllStages();
	Host->UnregisterDirector(Director);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaRegressionGasStagePause, "DreamNarrative.Regression.Integration.GasStagePausesChannel", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDnaRegressionGasStagePause::RunTest(const FString& Parameters)
{
	FDnaTestWorld Definitions(*this);
	if (!Definitions.CompileText(TEXT(
		"character Speaker\nchannel Radio\n  advance: auto\n  stage: pause\n"
		"dialogue Broadcast for Speaker channel Radio\n  Speaker: First line. for 1s\n  Speaker: Second line. for 1s\n"
		"stage AbilityStage\n  duration 5.0s\n"))) { return false; }
	FDnaStandaloneGame Game;
	TSharedRef<FDnaManualClock> Clock = MakeShared<FDnaManualClock>();
	Game.GetLedger()->SetClock(Clock);
	for (UDreamNarrativeScript* Script : Definitions.GetScripts()) { Game.GetStory()->RegisterScript(Script); }
	AActor* Avatar = Game.GetWorld()->SpawnActor<AActor>();
	UAbilitySystemComponent* System = NewObject<UAbilitySystemComponent>(Avatar);
	System->RegisterComponent();
	System->InitAbilityActorInfo(Avatar, Avatar);
	UDreamNarrativeGASBridge* Bridge = Game.GetGameInstance()->GetSubsystem<UDreamNarrativeGASBridge>();
	UDreamNarrativeDialogueRunner* Runner = Game.GetStory()->TalkTo(TEXT("Speaker"));
	if (!TestNotNull(TEXT("radio is talking"), Runner)) { return false; }
	TestTrue(TEXT("GAS stage starts"), Bridge->PlayStageFor(System, TEXT("AbilityStage")));
	UDreamNarrativeStageDirector* Director = Avatar->FindComponentByClass<UDreamNarrativeStageDirector>();
	TestTrue(TEXT("stage pause applies to GAS stages"), Runner->IsPaused());
	Clock->Advance(1.1);
	TestEqual(TEXT("radio stays on its first line during the stage"), Runner->GetCurrentLine().Text.ToString(), FString(TEXT("First line.")));
	if (Director) { Director->Stop(); }
	TestFalse(TEXT("stage ending resumes the channel"), Runner->IsPaused());
	Game.GetStory()->StopAllDialogue();
	return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaRegressionStandaloneStageActivity, "DreamNarrative.Regression.Integration.HostLedgerHearsStages", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDnaRegressionStandaloneStageActivity::RunTest(const FString& Parameters)
{
	// A ledger of its own, as a tool or a test keeps, and a director in a game whose game instance
	// keeps another: told which one to report to, the director reports there.
	FDnaTestWorld World(*this);
	if (!World.CompileText(TEXT(
		"character Speaker\nchannel Radio\n  advance: auto\n  stage: pause\n"
		"dialogue Broadcast for Speaker channel Radio\n  Speaker: First line. for 1s\n  Speaker: Second line. for 1s\n"
		"stage Long\n  duration 5.0s\n"))) { return false; }
	const UDnaStageSection* Section = World.GetScripts().Num() > 0 ? World.GetScripts()[0]->FindSection<UDnaStageSection>() : nullptr;
	const FDnaStageData* Long = Section ? Section->FindStage(TEXT("Long")) : nullptr;
	if (!TestNotNull(TEXT("stage compiled"), Long)) { return false; }
	FDnaStandaloneGame Game;
	UDreamNarrativeStageDirector* Director = UDreamNarrativeStageDirector::FindOrAddDirector(Game.GetWorld()->SpawnActor<AActor>());
	Director->bManualAdvance = true;
	UDreamNarrativeDialogueRunner* Runner = World.Story().TalkTo(TEXT("Speaker"));
	if (!TestNotNull(TEXT("radio is talking"), Runner)) { return false; }
	Director->ReportActivityTo(&World.Ledger());
	TestTrue(TEXT("the stage plays"), Director->Play(*Long) != 0);
	TestTrue(TEXT("the ledger it was told of hears of the stage"), Runner->IsPaused());
	Director->Stop();
	TestFalse(TEXT("and of its end"), Runner->IsPaused());
	World.Story().StopAllDialogue();
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaRegressionStagingPrefix,"DreamNarrative.Regression.Integration.StagingPrefixCollision", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDnaRegressionStagingPrefix::RunTest(const FString& Parameters)
{
	using namespace UE::DreamNarrative::Lang;
	FDnaEditSession Session;
	Session.Open(TEXT("ReviewPrefix.dna"), TEXT(
		"character Speaker\ndialogue Intro for Speaker\n  Speaker: First.\n"
		"dialogue Intro_Extra for Speaker\n  Speaker: Second.\n"));
	FText Error;
	if (!TestTrue(TEXT("stage longer dialogue name"), StageDnaDialogue(Session, TEXT("Intro_Extra"), 1, Error))) { return false; }
	const FString OtherStage = GetDnaStagingPrefix(TEXT("Intro_Extra")) + TEXT("Speaker");
	if (!TestNotNull(TEXT("longer dialogue stage exists"), Session.FindDeclaration(EDnaLineKind::Stage, OtherStage))) { return false; }
	FDnaTestWorld Before(*this);
	if (!Before.CompileText(Session.GetText())) { return false; }
	if (!TestTrue(TEXT("stage shorter dialogue name"), StageDnaDialogue(Session, TEXT("Intro"), 1, Error))) { return false; }
	TestNotNull(TEXT("generating Intro must preserve Intro_Extra's stage"), Session.FindDeclaration(EDnaLineKind::Stage, OtherStage));
	TestTrue(TEXT("other dialogue still references its generated stage"), Session.GetText().Contains(TEXT("@") + OtherStage));
	FDnaTestWorld After(*this);
	TestTrue(TEXT("both staged dialogues still compile"), After.CompileText(Session.GetText()));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaRegressionStagingKeepsPlayed, "DreamNarrative.Regression.Integration.StagingKeepsPlayedStage", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDnaRegressionStagingKeepsPlayed::RunTest(const FString& Parameters)
{
	using namespace UE::DreamNarrative::Lang;
	// A stage with Intro's generated name, made into one of its own and played by another dialogue.
	const FString Played = GetDnaStagingPrefix(TEXT("Intro")) + TEXT("Speaker");
	const FString Source = TEXT("character Speaker\nstage ") + Played + TEXT("\n  duration 1.0s\n")
		TEXT("dialogue Other for Speaker\n  play ") + Played + TEXT("\n  Speaker: Again.\n")
		TEXT("dialogue Intro for Speaker\n  Speaker: First.\n");
	FDnaTestWorld Before(*this);
	if (!Before.CompileText(Source)) { return false; }
	FDnaEditSession Session;
	Session.Open(TEXT("PlayedStage.dna"), Source);
	FText Error;
	TestFalse(TEXT("staging Intro refuses to take a stage another dialogue plays"), StageDnaDialogue(Session, TEXT("Intro"), 1, Error));
	TestEqual(TEXT("the file is left as it was"), Session.GetText(), Source);
	return !HasAnyErrors();
}
#endif
