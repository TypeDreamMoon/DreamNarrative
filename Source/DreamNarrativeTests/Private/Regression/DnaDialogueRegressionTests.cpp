// Copyright DreamMoon. All Rights Reserved.
#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "DnaDialogueRegressionProbe.h"
#include "DreamNarrativeLedger.h"
#include "DreamNarrativeStageSubsystem.h"
#include "DreamNarrativeStory.h"
#include "Fixtures/DnaTestWorld.h"
#include "UObject/StrongObjectPtr.h"

using namespace UE::DreamNarrative::Tests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaRegressionPausedStage, "DreamNarrative.Regression.Dialogue.PausedStageCompletion", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDnaRegressionPausedStage::RunTest(const FString& Parameters)
{
	FDnaTestWorld World(*this);
	UDreamNarrativeStageSubsystem* Host = World.EnableStages();
	if (!World.CompileText(TEXT(
		"character Speaker\nfact Applied: int\nchannel main\n  pauses: Radio\nchannel Radio\n"
		"stage Short\n  duration 1.0s\n"
		"dialogue RadioScene channel Radio\n  play Short\n  set Applied += 1\n  Speaker: Radio continued.\n"
		"dialogue MainScene for Speaker\n  Speaker: Main is speaking.\n"))) { return false; }
	UDreamNarrativeDialogueRunner* Radio = World.Story().StartDialogue(TEXT("RadioScene"));
	if (!TestNotNull(TEXT("radio started"), Radio)) { return false; }
	TestEqual(TEXT("radio waits on stage"), Radio->GetState(), EDnaRunnerState::PlayingStage);
	UDreamNarrativeDialogueRunner* Main = World.Story().StartDialogue(TEXT("MainScene"));
	if (!TestNotNull(TEXT("main started"), Main)) { return false; }
	TestTrue(TEXT("main pauses radio"), Radio->IsPaused());
	World.Clock().Advance(1.1);
	TestEqual(TEXT("stage did end"), Host->NumPlaying(), 0);
	TestTrue(TEXT("main still pauses radio"), Radio->IsPaused());
	TestEqual(TEXT("paused radio must defer its post-stage writes"), World.Ledger().GetFactAsInt(TEXT("Applied")), 0);
	Main->Advance();
	TestFalse(TEXT("main ending resumes radio"), Radio->IsPaused());
	TestEqual(TEXT("resuming executes post-stage command once"), World.Ledger().GetFactAsInt(TEXT("Applied")), 1);
	World.Story().StopAllDialogue();
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaRegressionStopAllStagePause, "DreamNarrative.Regression.Dialogue.StopAllStagePause", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDnaRegressionStopAllStagePause::RunTest(const FString& Parameters)
{
	FDnaTestWorld World(*this);
	UDreamNarrativeStageSubsystem* Host = World.EnableStages();
	if (!World.CompileText(TEXT(
		"character Speaker\nchannel Radio\n  advance: auto\n  stage: pause\n"
		"stage Long\n  duration 5.0s\n"
		"dialogue RadioScene channel Radio\n  Speaker: First. for 1s\n  Speaker: Second. for 1s\n"))) { return false; }
	UDreamNarrativeDialogueRunner* Old = World.Story().StartDialogue(TEXT("RadioScene"));
	if (!TestNotNull(TEXT("first radio started"), Old)) { return false; }
	FDnaStageRequest Request;
	Request.Stage = TEXT("Long");
	const uint64 Ticket = Host->PlayStage(Request, FDnaStageFinishedDelegate());
	TestTrue(TEXT("stage started"), Ticket != 0);
	TestTrue(TEXT("stage initially pauses radio"), Old->IsPaused());
	World.Story().StopAllDialogue();
	TestTrue(TEXT("independent stage survives dialogue stop"), Host->IsStagePlaying(Ticket));
	UDreamNarrativeDialogueRunner* New = World.Story().StartDialogue(TEXT("RadioScene"));
	if (!TestNotNull(TEXT("new radio started"), New)) { return false; }
	TestTrue(TEXT("new radio must still be paused by the active stage"), New->IsPaused());
	World.Clock().Advance(1.1);
	TestTrue(TEXT("long stage still playing"), Host->IsStagePlaying(Ticket));
	TestEqual(TEXT("radio cannot advance during active stage"), New->GetCurrentLine().Text.ToString(), FString(TEXT("First.")));
	Host->StopStage(Ticket);
	World.Story().StopAllDialogue();
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaRegressionChoiceAbort, "DreamNarrative.Regression.Dialogue.ChoiceCallbackAbort", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDnaRegressionChoiceAbort::RunTest(const FString& Parameters)
{
	FDnaTestWorld World(*this);
	if (!World.CompileText(TEXT("character Speaker\nfact Applied: int\ndialogue Decision for Speaker\n  - Choose.\n    set Applied += 1\n"))) { return false; }
	UDreamNarrativeDialogueRunner* Runner = World.Story().StartDialogue(TEXT("Decision"));
	if (!TestNotNull(TEXT("decision started"), Runner)) { return false; }
	TestEqual(TEXT("choice is offered"), Runner->GetState(), EDnaRunnerState::AwaitingChoice);
	TStrongObjectPtr<UDnaDialogueRegressionProbe> Probe(NewObject<UDnaDialogueRegressionProbe>());
	Runner->OnChoiceMade.AddDynamic(Probe.Get(), &UDnaDialogueRegressionProbe::AbortWhenChosen);
	Runner->OnEnded.AddDynamic(Probe.Get(), &UDnaDialogueRegressionProbe::CountEnd);
	Runner->Choose(0);
	TestEqual(TEXT("abort prevents choice branch side effects"), World.Ledger().GetFactAsInt(TEXT("Applied")), 0);
	TestEqual(TEXT("aborted dialogue ends exactly once"), Probe->Ends, 1);
	TestEqual(TEXT("aborted dialogue records one visit"), World.Ledger().GetVisited(TEXT("Decision")), 1);
	Runner->OnChoiceMade.RemoveDynamic(Probe.Get(), &UDnaDialogueRegressionProbe::AbortWhenChosen);
	Runner->OnEnded.RemoveDynamic(Probe.Get(), &UDnaDialogueRegressionProbe::CountEnd);
	World.Story().StopAllDialogue();
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaRegressionControls, "DreamNarrative.Regression.Dialogue.ControlPaths", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDnaRegressionControls::RunTest(const FString& Parameters)
{
	FDnaTestWorld World(*this);
	World.EnableStages();
	if (!World.CompileText(TEXT(
		"character Speaker\nfact Applied: int\nstage Short\n  duration 1.0s\n"
		"dialogue StageScene\n  play Short\n  set Applied += 1\n  Speaker: Done.\n"
		"dialogue WaitScene\n  wait 1.0s\n  set Applied += 1\n  Speaker: Done.\n"
		"dialogue Decision for Speaker\n  - Choose.\n    set Applied += 1\n"))) { return false; }
	UDreamNarrativeDialogueRunner* Stage = World.Story().StartDialogue(TEXT("StageScene"));
	if (!TestNotNull(TEXT("stage scene"), Stage)) { return false; }
	World.Clock().Advance(1.1);
	TestEqual(TEXT("unpaused stage resumes normally"), World.Ledger().GetFactAsInt(TEXT("Applied")), 1);
	UDreamNarrativeDialogueRunner* Wait = World.Story().StartDialogue(TEXT("WaitScene"));
	if (!TestNotNull(TEXT("wait scene"), Wait)) { return false; }
	Wait->Pause();
	World.Clock().Advance(2.0);
	TestEqual(TEXT("ordinary wait obeys pause"), World.Ledger().GetFactAsInt(TEXT("Applied")), 1);
	Wait->Resume();
	World.Clock().Advance(1.1);
	TestEqual(TEXT("resumed wait runs once"), World.Ledger().GetFactAsInt(TEXT("Applied")), 2);
	UDreamNarrativeDialogueRunner* Choice = World.Story().StartDialogue(TEXT("Decision"));
	if (!TestNotNull(TEXT("choice scene"), Choice)) { return false; }
	TestTrue(TEXT("ordinary choice succeeds"), Choice->Choose(0));
	TestEqual(TEXT("ordinary choice executes branch once"), World.Ledger().GetFactAsInt(TEXT("Applied")), 3);
	TestEqual(TEXT("ordinary choice records one visit"), World.Ledger().GetVisited(TEXT("Decision")), 1);
	World.Story().StopAllDialogue();
	return !HasAnyErrors();
}
#endif
