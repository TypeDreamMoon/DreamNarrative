// Copyright DreamMoon. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DreamNarrativeDialogueRunner.h"
#include "DreamNarrativeLedger.h"
#include "DreamNarrativeScript.h"
#include "DreamNarrativeStory.h"
#include "Fixtures/DnaGameFixture.h"
#include "Fixtures/DnaTestWorld.h"
#include "Internationalization/Culture.h"
#include "Internationalization/GatherableTextData.h"
#include "Internationalization/Internationalization.h"
#include "Internationalization/PolyglotTextData.h"
#include "Internationalization/TextLocalizationManager.h"
#include "Serialization/PropertyLocalizationDataGathering.h"
#include "UObject/UObjectIterator.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaLocalizationTest, "DreamNarrative.Lang.Localization", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDnaLocalizationTest::RunTest(const FString& Parameters)
{
	using namespace UE::DreamNarrative::Tests;

	const FString Sample = TEXT("Tests/Corpus/Validate/Samples/automata.dna");
	const FString Namespace = TEXT("DNA/automata");

	// -- gathering: the script packages hold their texts the way the engine's gatherer collects them,
	// under the file's namespace and keys that do not depend on where a line sits
	TArray<FGatherableTextData> Gathered;
	{
		FDnaScratchAssets Assets(*this, { Sample });
		if (!Assets.IsValid())
		{
			return false;
		}
		for (TObjectIterator<UDreamNarrativeScript> It; It; ++It)
		{
			if (It->GetPackage()->GetName().StartsWith(Assets.GetDirectory()))
			{
				EPropertyLocalizationGathererResultFlags Flags = EPropertyLocalizationGathererResultFlags::Empty;
				FPropertyLocalizationDataGatherer(Gathered, It->GetPackage(), Flags);
			}
		}
	}
	auto KeyOf = [&Gathered, &Namespace](const FString& Source, const FString& Key = FString()) -> FString
	{
		for (const FGatherableTextData& Data : Gathered)
		{
			if (Data.NamespaceName != Namespace || Data.SourceData.SourceString != Source)
			{
				continue;
			}
			for (const FTextSourceSiteContext& Site : Data.SourceSiteContexts)
			{
				if (Key.IsEmpty() || Site.KeyName == Key)
				{
					return Site.KeyName;
				}
			}
		}
		return FString();
	};
	const FString LineKey = KeyOf(TEXT("结束了吗……?"));
	TestFalse(TEXT("a line of dialogue is gathered"), LineKey.IsEmpty());
	TestFalse(TEXT("a choice"), KeyOf(TEXT("别大意。")).IsEmpty());
	TestFalse(TEXT("a quest's title"), KeyOf(TEXT("迷途的机械")).IsEmpty());
	TestFalse(TEXT("a journal line"), KeyOf(TEXT("有台机械说想吃鱼。")).IsEmpty());
	TestFalse(TEXT("a stage's text, under its stage"), KeyOf(TEXT("这里也有……一样的东西"), TEXT("stage.骇入闪回.text0")).IsEmpty());

	// -- a translation: registered for the line, the dialogue shows it in that language, and the source again after
	FDnaTestWorld World(*this);
	if (!World.CompilePluginFiles({ Sample }) || LineKey.IsEmpty())
	{
		return false;
	}
	FDnaEventPayload Defeated;
	Defeated.Event = TEXT("Boss击破");
	World.Ledger().RaiseEvent(Defeated);
	UDreamNarrativeDialogueRunner* Runner = World.Story().GetActiveRunner();
	if (!TestNotNull(TEXT("the dialogue after the boss runs"), Runner))
	{
		return false;
	}
	TestEqual(TEXT("in the source language"), Runner->GetCurrentLine().Text.ToString(), FString(TEXT("结束了吗……?")));

	// A translation for the line's namespace and key. A game ships it in its .locres; here it is registered
	// in memory, under a category that follows the current language even in the editor (game text there
	// follows the project's preview language setting instead).
	const FString Previous = FInternationalization::Get().GetCurrentLanguage()->GetName();
	FPolyglotTextData Translation(ELocalizedTextSourceCategory::Engine, Namespace, LineKey, TEXT("结束了吗……?"));
	Translation.AddLocalizedString(TEXT("fr"), TEXT("C'est fini... ?"));
	FTextLocalizationManager::Get().RegisterPolyglotTextData(Translation);
	if (TestTrue(TEXT("switch to French"), FInternationalization::Get().SetCurrentLanguage(TEXT("fr"))))
	{
		TestEqual(TEXT("the line in French"), Runner->GetCurrentLine().Text.ToString(), FString(TEXT("C'est fini... ?")));
		FInternationalization::Get().SetCurrentLanguage(Previous);
	}
	TestEqual(TEXT("and back"), Runner->GetCurrentLine().Text.ToString(), FString(TEXT("结束了吗……?")));
	return !HasAnyErrors();
}

#endif
