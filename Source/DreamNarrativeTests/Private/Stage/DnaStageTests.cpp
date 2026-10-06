// Copyright DreamMoon. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DreamNarrativeScript.h"
#include "DreamNarrativeStageData.h"
#include "DreamNarrativeStagePose.h"
#include "Fixtures/DnaTestWorld.h"
#include "Internationalization/TextNamespaceUtil.h"

namespace UE::DreamNarrative::Tests::StageTests
{
	const FDnaStageData* FindStage(const FDnaTestWorld& World, FName Name)
	{
		for (const UDreamNarrativeScript* Script : World.GetScripts())
		{
			const UDnaStageSection* Section = Script ? Script->FindSection<UDnaStageSection>() : nullptr;
			if (const FDnaStageData* Stage = Section ? Section->FindStage(Name) : nullptr)
			{
				return Stage;
			}
		}
		return nullptr;
	}

	FDnaStageEntry MaskEntry(EDnaStageVerb Verb, float Start, float End, bool bRange, float Over = 0.f)
	{
		FDnaStageEntry Entry;
		Entry.Track = EDnaStageTrack::Mask;
		Entry.Verb = Verb;
		Entry.Start = Start;
		Entry.End = End;
		Entry.bRange = bRange;
		Entry.Over = Over;
		return Entry;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaStageCompileTest, "DreamNarrative.Stage.Compile", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDnaStageCompileTest::RunTest(const FString& Parameters)
{
	using namespace UE::DreamNarrative::Tests;
	using namespace UE::DreamNarrative::Tests::StageTests;

	FDnaTestWorld World(*this);
	if (!World.CompilePluginFiles({ TEXT("Tests/Corpus/Validate/Samples/automata.dna") }))
	{
		return false;
	}

	// -- an absolute stage: seconds, its shots, its entries by time, its restore block
	const FDnaStageData* Finisher = FindStage(World, TEXT("处决_巨型机械"));
	if (!TestNotNull(TEXT("the finisher compiled"), Finisher))
	{
		return false;
	}
	TestFalse(TEXT("it counts in seconds"), Finisher->bLineRelative);
	TestEqual(TEXT("for four of them"), Finisher->Duration, 4.f);
	TestEqual(TEXT("an ability plays it"), Finisher->GameplayTag, FName(TEXT("能力.处决")));
	TestEqual(TEXT("two shots"), Finisher->Shots.Num(), 2);
	if (const FDnaStageShot* Side = Finisher->FindShot(TEXT("侧面")))
	{
		TestEqual(TEXT("placed by the machine"), Side->Anchor, FName(TEXT("巨型机械")));
		TestEqual(TEXT("where"), Side->Offset, FVector(0.0, -600.0, 150.0));
		TestEqual(TEXT("its field of view"), Side->Fov, 50.f);
	}
	bool bSorted = true;
	for (int32 Index = 1; Index < Finisher->Entries.Num(); ++Index)
	{
		bSorted &= Finisher->Entries[Index - 1].Start <= Finisher->Entries[Index].Start;
	}
	TestTrue(TEXT("the entries go by time, whatever track they were written in"), bSorted);
	TestEqual(TEXT("every entry"), Finisher->Entries.Num(), 15);
	const FDnaStageEntry* Montage = Finisher->Entries.FindByPredicate([](const FDnaStageEntry& Entry) { return Entry.Verb == EDnaStageVerb::Montage; });
	if (TestNotNull(TEXT("the montage"), Montage))
	{
		TestEqual(TEXT("by name"), Montage->Asset, FString(TEXT("AM_二B_处决")));
		TestEqual(TEXT("on 2B"), Montage->Target, FName(TEXT("二B")));
		TestEqual(TEXT("warped to the machine"), Montage->WarpActor, FName(TEXT("巨型机械")));
	}
	const FDnaStageEntry* Spawn = Finisher->Entries.FindByPredicate([](const FDnaStageEntry& Entry) { return Entry.Verb == EDnaStageVerb::Spawn; });
	if (TestNotNull(TEXT("the effect"), Spawn))
	{
		TestEqual(TEXT("by its path"), Spawn->Asset, FString(TEXT("/Game/FX/NS_处决_斩击")));
	}
	const FDnaStageEntry* Ramp = Finisher->Entries.FindByPredicate([](const FDnaStageEntry& Entry) { return Entry.Verb == EDnaStageVerb::RampDark; });
	if (TestNotNull(TEXT("the lights going down"), Ramp))
	{
		TestEqual(TEXT("over"), Ramp->Over, 0.4f);
		TestEqual(TEXT("to"), Ramp->Amount, 0.3f);
	}
	TestEqual(TEXT("the restore block"), Finisher->Restore.Num(), 1);
	TestEqual(TEXT("its blend back runs half a second past the end"), Finisher->GetTail(), 0.5f);

	// -- a stage on a line: fractions of the line, its text made localisable
	const FDnaStageData* Flash = FindStage(World, TEXT("骇入闪回"));
	if (!TestNotNull(TEXT("the flash compiled"), Flash))
	{
		return false;
	}
	TestTrue(TEXT("it follows its line"), Flash->bLineRelative);
	TestEqual(TEXT("which is all of it"), Flash->Duration, 1.f);
	const FDnaStageEntry* Text = Flash->Entries.FindByPredicate([](const FDnaStageEntry& Entry) { return Entry.Verb == EDnaStageVerb::Text; });
	if (TestNotNull(TEXT("its text"), Text))
	{
		TestEqual(TEXT("from"), Text->Start, 0.12f);
		TestEqual(TEXT("to"), Text->End, 0.3f);
		TestEqual(TEXT("the words"), Text->Text.ToString(), FString(TEXT("这里也有……一样的东西")));
		TestEqual(TEXT("the style"), Text->Style, FName(TEXT("骇入")));
#if WITH_EDITOR
		const TOptional<FString> Key = FTextInspector::GetKey(Text->Text);
		TestEqual(TEXT("localisable under its stage"), Key.Get(FString()), FString(TEXT("stage.骇入闪回.text0")));
#endif
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaStagePoseTest, "DreamNarrative.Stage.Pose", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDnaStagePoseTest::RunTest(const FString& Parameters)
{
	using namespace UE::DreamNarrative::Tests;
	using namespace UE::DreamNarrative::Tests::StageTests;
	using DreamNarrative::SampleDnaStage;

	FDnaTestWorld World(*this);
	if (!World.CompilePluginFiles({ TEXT("Tests/Corpus/Validate/Samples/automata.dna") }))
	{
		return false;
	}
	const FDnaStageData* Finisher = FindStage(World, TEXT("处决_巨型机械"));
	const FDnaStageData* Flash = FindStage(World, TEXT("骇入闪回"));
	if (!TestNotNull(TEXT("the finisher"), Finisher) || !TestNotNull(TEXT("the flash"), Flash))
	{
		return false;
	}

	// -- the start: the camera is taken, the HUD gone, the player held
	FDnaStagePose Pose = SampleDnaStage(*Finisher, 0.f);
	TestTrue(TEXT("0 s: the stage has the camera"), Pose.Camera.bActive);
	TestEqual(TEXT("0 s: blending in a quarter second"), Pose.Camera.BlendIn, 0.25f);
	TestEqual(TEXT("0 s: from where the game's view was"), Pose.Camera.To, FName());
	TestTrue(TEXT("0 s: no HUD"), Pose.bHudHidden);
	TestTrue(TEXT("0 s: no input"), Pose.bInputLocked);
	TestTrue(TEXT("0 s: hits count"), Pose.bDamageWindow);
	TestEqual(TEXT("0 s: the lights start going down"), Pose.LightShare, 1.f);

	Pose = SampleDnaStage(*Finisher, 0.2f);
	TestEqual(TEXT("0.2 s: half way down"), Pose.LightShare, 0.65f, 1e-4f);

	// -- the first cut, under the distortion at its strongest
	Pose = SampleDnaStage(*Finisher, 1.f);
	TestEqual(TEXT("1 s: one cut"), Pose.Camera.Cuts, 1);
	TestEqual(TEXT("1 s: to the side"), Pose.Camera.To, FName(TEXT("侧面")));
	TestEqual(TEXT("1 s: the distortion at full"), Pose.Distort, 0.8f, 1e-3f);
	TestEqual(TEXT("1.05 s: and going"), SampleDnaStage(*Finisher, 1.05f).Distort, 0.4f, 1e-3f);

	// -- the second cut under the white flash, the hit stopping the world, the push starting
	Pose = SampleDnaStage(*Finisher, 2.45f);
	TestEqual(TEXT("2.45 s: two cuts"), Pose.Camera.Cuts, 2);
	TestEqual(TEXT("2.45 s: on the close-up"), Pose.Camera.To, FName(TEXT("特写")));
	TestEqual(TEXT("2.45 s: the flash fading"), Pose.White, 0.5f, 1e-3f);
	TestEqual(TEXT("2.45 s: the world at a tenth"), Pose.TimeDilation, 0.1f, 1e-5f);
	TestTrue(TEXT("2.45 s: pushing in"), Pose.Camera.Push > 0.f && Pose.Camera.Push < 60.f);
	TestFalse(TEXT("2.45 s: hits no longer count"), Pose.bDamageWindow);
	TestEqual(TEXT("3.6 s: the push done"), SampleDnaStage(*Finisher, 3.6f).Camera.Push, 60.f, 1e-3f);

	// -- the end: the lights back, the blend back under way, the restore block applied
	Pose = SampleDnaStage(*Finisher, 4.f);
	TestEqual(TEXT("4 s: the lights back"), Pose.LightShare, 1.f, 1e-4f);
	TestTrue(TEXT("4 s: going back to the game"), Pose.Camera.bActive && Pose.Camera.bReturning);
	TestEqual(TEXT("4 s: over half a second"), Pose.Camera.BlendOut, 0.5f);
	TestFalse(TEXT("4 s: the HUD back (restore)"), Pose.bHudHidden);
	TestFalse(TEXT("4 s: the input back"), Pose.bInputLocked);
	Pose = SampleDnaStage(*Finisher, 4.5f);
	TestFalse(TEXT("4.5 s: the camera given back"), Pose.Camera.bActive);
	TestEqual(TEXT("4.5 s: at rest"), Pose.Describe(), FString(TEXT("at rest")));

	// -- a stage on a line: the same fractions over any length of line
	Pose = SampleDnaStage(*Flash, 0.2f, 3.f);
	TestEqual(TEXT("20%: the comic look"), Pose.Comic, 1.f);
	TestEqual(TEXT("20%: the text"), Pose.Text.ToString(), FString(TEXT("这里也有……一样的东西")));
	TestEqual(TEXT("20%: in its style"), Pose.TextStyle, FName(TEXT("骇入")));
	Pose = SampleDnaStage(*Flash, 0.32f, 3.f);
	TestEqual(TEXT("32%: black"), Pose.Black, 1.f);
	TestTrue(TEXT("32%: the text gone"), Pose.Text.IsEmpty());
	TestTrue(TEXT("50%: nothing left"), !SampleDnaStage(*Flash, 0.5f, 3.f).HasMask());

	// -- the envelopes the validator checks cuts against
	using DreamNarrative::GetDnaMaskEnvelope;
	const FDnaStageEntry Pulse = MaskEntry(EDnaStageVerb::Distort, 1.9f, 2.1f, true);
	TestEqual(TEXT("a pulse peaks in its middle"), GetDnaMaskEnvelope(Pulse, 2.f), 1.f, 1e-4f);
	TestEqual(TEXT("and is nothing at its ends"), GetDnaMaskEnvelope(Pulse, 1.9f), 0.f, 1e-4f);
	const FDnaStageEntry Fade = MaskEntry(EDnaStageVerb::FadeBlack, 4.4f, 4.6f, true, 0.1f);
	TestEqual(TEXT("a fade is full after fading in"), GetDnaMaskEnvelope(Fade, 4.5f), 1.f, 1e-4f);
	TestEqual(TEXT("half way in"), GetDnaMaskEnvelope(Fade, 4.45f), 0.5f, 1e-3f);
	const FDnaStageEntry Black = MaskEntry(EDnaStageVerb::CutBlack, 1.f, 2.f, true);
	TestEqual(TEXT("a cut to black is full from its start"), GetDnaMaskEnvelope(Black, 1.f), 1.f);
	TestEqual(TEXT("and gone at its end"), GetDnaMaskEnvelope(Black, 2.f), 0.f);
	const FDnaStageEntry Held = MaskEntry(EDnaStageVerb::FadeWhite, 1.f, 1.f, false, 0.5f);
	TestEqual(TEXT("a fade at a moment holds"), GetDnaMaskEnvelope(Held, 9.f), 1.f);
	return !HasAnyErrors();
}

#endif
