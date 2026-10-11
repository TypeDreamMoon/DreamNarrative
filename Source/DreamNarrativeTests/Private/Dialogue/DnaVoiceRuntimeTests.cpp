// Copyright DreamMoon. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AssetRegistry/AssetRegistryModule.h"
#include "DnaVoiceProbe.h"
#include "DreamNarrativeSettings.h"
#include "DreamNarrativeStory.h"
#include "Fixtures/DnaTestWorld.h"
#include "Misc/Guid.h"
#include "Misc/ScopeExit.h"
#include "Sound/SoundWave.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
	/** Voices for 甲's lines under one root, each running Seconds: `<root>/甲/<id>`. */
	struct FVoices
	{
		FString Root = TEXT("/Temp/DnaVoiceRun_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
		TArray<UObject*> Made;

		explicit FVoices(std::initializer_list<const TCHAR*> Ids, float Seconds = 2.f)
		{
			for (const TCHAR* Id : Ids)
			{
				const FString Name = FString(Id).Replace(TEXT("."), TEXT("_"));
				USoundWave* Wave = NewObject<USoundWave>(CreatePackage(*(Root / TEXT("甲") / Name)), *Name, RF_Public | RF_Standalone);
				Wave->Duration = Seconds;
				FAssetRegistryModule::AssetCreated(Wave);
				Made.Add(Wave);
			}
		}

		~FVoices()
		{
			for (UObject* Object : Made)
			{
				FAssetRegistryModule::AssetDeleted(Object);
				Object->ClearFlags(RF_Standalone);
			}
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaVoiceEventsTest, "DreamNarrative.Dialogue.VoiceEvents", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDnaVoiceEventsTest::RunTest(const FString& Parameters)
{
	using namespace UE::DreamNarrative::Tests;

	const FVoices Voices({ TEXT("正事.1"), TEXT("正事.2"), TEXT("正事.3"), TEXT("播报.1"), TEXT("播报.2"), TEXT("播报.3"), TEXT("插话.1") });
	FDnaTestWorld World(*this);
	const bool bCompiled = World.CompileText(FString::Printf(TEXT(
		"character 甲\n"
		"  voice: %s/甲\n"
		"character 乙\n"
		"channel main\n"
		"  pauses: 电台\n"
		"channel 电台\n"
		"channel 插话频道\n"
		"dialogue 正事 for 甲\n"
		"  甲: 一。\n"
		"  甲: 二。\n"
		"  甲: 三。\n"
		"dialogue 播报 channel 电台\n"
		"  甲: 一。\n"
		"  甲: 二。\n"
		"  甲: 三。\n"
		"dialogue 插话 channel 插话频道\n"
		"  甲: 插一句。\n"
		"dialogue 乙说 for 乙\n"
		"  乙: 我没有声音。\n"), *Voices.Root));
	if (!bCompiled) { return false; }
	UDreamNarrativeStory& Story = World.Story();
	TStrongObjectPtr<UDnaVoiceProbe> Probe(NewObject<UDnaVoiceProbe>());
	Story.OnVoice.AddDynamic(Probe.Get(), &UDnaVoiceProbe::Hear);

	// The player moves on: each line asks for its voice, and moving on before it ran out stops it.
	UDreamNarrativeDialogueRunner* Talk = Story.StartDialogue(TEXT("正事"));
	if (!TestNotNull(TEXT("the talk started"), Talk)) { return false; }
	TestEqual(TEXT("the first line asks for its voice"), Probe->Take(), FString(TEXT("Requested 正事.1")));
	TestEqual(TEXT("which runs as long as the asset says"), Talk->GetCurrentLine().VoiceSeconds, 2.f);
	Talk->Advance();
	TestEqual(TEXT("moving on cuts it"), Probe->Take(), FString(TEXT("Stopped 正事.1, Requested 正事.2")));
	Talk->NotifyVoiceFinished();
	TestEqual(TEXT("the game says it ran out"), Probe->Take(), FString(TEXT("Finished 正事.2")));
	TestEqual(TEXT("the player's line stays up"), Talk->GetCurrentLine().Id, FName(TEXT("正事.2")));
	Talk->Advance();
	TestEqual(TEXT("nothing left to stop"), Probe->Take(), FString(TEXT("Requested 正事.3")));
	Talk->Abort();
	TestEqual(TEXT("ending the dialogue stops it"), Probe->Take(), FString(TEXT("Stopped 正事.3")));

	// A channel that moves on by itself waits for the voice, and a little more.
	UDreamNarrativeDialogueRunner* Radio = Story.StartDialogue(TEXT("播报"));
	if (!TestNotNull(TEXT("the radio started"), Radio)) { return false; }
	TestEqual(TEXT("its first line"), Probe->Take(), FString(TEXT("Requested 播报.1")));
	World.Clock().Advance(2.f);
	TestEqual(TEXT("still on it while the voice runs"), Radio->GetCurrentLine().Id, FName(TEXT("播报.1")));
	World.Clock().Advance(UDreamNarrativeSettings::Get()->VoiceTailSeconds + 0.01f);
	TestEqual(TEXT("then it moves on, the voice over"), Probe->Take(), FString(TEXT("Finished 播报.1, Requested 播报.2")));
	Radio->NotifyVoiceFinished();
	TestEqual(TEXT("the game's word moves it on at once"), Probe->Take(), FString(TEXT("Finished 播报.2, Requested 播报.3")));

	// The same mouth on another channel: the radio's voice stops, its line stays.
	UDreamNarrativeDialogueRunner* Aside = Story.StartDialogue(TEXT("插话"));
	if (!TestNotNull(TEXT("the aside started"), Aside)) { return false; }
	TestEqual(TEXT("the speaker stops talking on the radio first"), Probe->Take(), FString(TEXT("Stopped 播报.3, Requested 插话.1")));
	TestEqual(TEXT("whose line stays up"), Radio->GetCurrentLine().Id, FName(TEXT("播报.3")));
	Aside->Abort();
	Radio->Abort();
	Probe->Take();

	// Paused, the line comes down with its voice; resumed, it is said again.
	UDreamNarrativeDialogueRunner* Again = Story.StartDialogue(TEXT("播报"));
	Probe->Take();
	UDreamNarrativeDialogueRunner* Main = Story.TalkTo(TEXT("乙"));
	if (!TestNotNull(TEXT("main started"), Main) || !TestNotNull(TEXT("radio again"), Again)) { return false; }
	TestTrue(TEXT("main holds the radio"), Again->IsPaused());
	TestEqual(TEXT("which stops its voice; a line with none asks for nothing"), Probe->Take(), FString(TEXT("Stopped 播报.1")));
	Main->Advance();
	TestEqual(TEXT("said again when main is quiet"), Probe->Take(), FString(TEXT("Requested 播报.1")));

	// The actor a character speaks from comes with each event.
	AActor* Mouth = NewObject<AActor>();
	Story.BindSpeaker(TEXT("甲"), Mouth);
	Again->Advance();
	TestTrue(TEXT("the bound speaker"), Probe->Speakers.Num() > 0 && Probe->Speakers.Last().Get() == Mouth);
	Story.StopAllDialogue();
	Story.OnVoice.RemoveDynamic(Probe.Get(), &UDnaVoiceProbe::Hear);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaVoiceWaitTest, "DreamNarrative.Dialogue.VoiceWaitsForTheGame", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDnaVoiceWaitTest::RunTest(const FString& Parameters)
{
	using namespace UE::DreamNarrative::Tests;

	UDreamNarrativeSettings* Settings = GetMutableDefault<UDreamNarrativeSettings>();
	const bool bWas = Settings->bWaitForVoiceFinished;
	ON_SCOPE_EXIT { Settings->bWaitForVoiceFinished = bWas; };
	Settings->bWaitForVoiceFinished = true;

	const FVoices Voices({ TEXT("播报.1"), TEXT("播报.2") });
	FDnaTestWorld World(*this);
	if (!World.CompileText(FString::Printf(TEXT(
		"character 甲\n  voice: %s/甲\n"
		"channel 电台\n"
		"dialogue 播报 channel 电台\n  甲: 一。\n  甲: 二。\n"), *Voices.Root))) { return false; }
	UDreamNarrativeDialogueRunner* Radio = World.Story().StartDialogue(TEXT("播报"));
	if (!TestNotNull(TEXT("the radio started"), Radio)) { return false; }
	World.Clock().Advance(3.f);
	TestEqual(TEXT("past the voice's length it still waits for the game"), Radio->GetCurrentLine().Id, FName(TEXT("播报.1")));
	Radio->NotifyVoiceFinished();
	TestEqual(TEXT("the game's word moves it on"), Radio->GetCurrentLine().Id, FName(TEXT("播报.2")));
	World.Clock().Advance(2.f + Settings->VoiceTailSeconds + Settings->VoiceWaitGraceSeconds + 0.01f);
	TestEqual(TEXT("and should the game never say, the grace runs out"), Radio->GetState(), EDnaRunnerState::Ended);
	return !HasAnyErrors();
}

#endif
