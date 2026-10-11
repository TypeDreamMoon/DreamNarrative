// Copyright DreamMoon. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DreamNarrativeDialogueRunner.h"
#include "DreamNarrativeLedger.h"
#include "DreamNarrativeScript.h"
#include "DreamNarrativeStory.h"
#include "Fixtures/DnaGameFixture.h"
#include "Fixtures/DnaTestWorld.h"
#include "HAL/FileManager.h"
#include "Internationalization/Culture.h"
#include "Internationalization/GatherableTextData.h"
#include "Internationalization/Internationalization.h"
#include "Internationalization/InternationalizationManifest.h"
#include "Internationalization/PolyglotTextData.h"
#include "Internationalization/TextLocalizationManager.h"
#include "Lang/DnaLocalization.h"
#include "LocTextHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaTranslationTableTest, "DreamNarrative.Lang.TranslationTable", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDnaTranslationTableTest::RunTest(const FString& Parameters)
{
	using namespace UE::DreamNarrative::Lang;
	using namespace UE::DreamNarrative::Tests;

	FDnaTestWorld World(*this);
	if (!World.CompilePluginFiles({ TEXT("Tests/Corpus/Validate/Samples/automata.dna") }))
	{
		return false;
	}
	TArray<UDreamNarrativeScript*> Scripts;
	for (UDreamNarrativeScript* Script : World.GetScripts()) { Scripts.Add(Script); }

	// -- the export: every text once, with who says it and where
	TArray<FDnaLocRow> Rows;
	CollectDnaLocTexts(Scripts, Rows);
	auto Find = [&Rows](const FString& Source) { return Rows.FindByPredicate([&Source](const FDnaLocRow& Row) { return Row.Source == Source; }); };
	const FDnaLocRow* Line = Find(TEXT("结束了吗……?"));
	const FDnaLocRow* Choice = Find(TEXT("别大意。"));
	if (!TestNotNull(TEXT("a line"), Line) || !TestNotNull(TEXT("a choice"), Choice))
	{
		return false;
	}
	TestEqual(TEXT("under the file's namespace"), Line->Namespace, FString(TEXT("DNA/automata")));
	TestTrue(TEXT("a quest's title too"), Find(TEXT("迷途的机械")) && Find(TEXT("迷途的机械"))->Kind == TEXT("text"));
	TestEqual(TEXT("a line is a line"), Line->Kind, FString(TEXT("line")));
	TestFalse(TEXT("with its dialogue"), Line->Dialogue.IsEmpty());
	TestTrue(TEXT("and where it is"), Line->File.EndsWith(TEXT("automata.dna")) && Line->Line > 0);
	TestEqual(TEXT("a choice is a choice"), Choice->Kind, FString(TEXT("choice")));
	TestTrue(TEXT("a stage's text, by its stable key"), Rows.ContainsByPredicate([](const FDnaLocRow& Row) { return Row.Key == TEXT("stage.骇入闪回.text0"); }));
	TSet<FString> Keys;
	for (const FDnaLocRow& Row : Rows) { Keys.Add(Row.Namespace + TEXT("|") + Row.Key); }
	TestEqual(TEXT("each text once"), Keys.Num(), Rows.Num());
	const FDnaLocRow* Spoken = Rows.FindByPredicate([](const FDnaLocRow& Row) { return Row.Kind == TEXT("line") && !Row.Character.IsEmpty() && !Row.Previous.IsEmpty(); });
	const FDnaLocRow* Title = Find(TEXT("迷途的机械"));
	if (!TestNotNull(TEXT("a line says who speaks it and what came before"), Spoken) || !TestNotNull(TEXT("the title"), Title))
	{
		return false;
	}

	// -- the CSV: what goes out comes back, commas, quotes and line breaks included
	TArray<FDnaLocRow> Out = Rows;
	Out[0].Translation = TEXT("one, \"two\"\nthree");
	TArray<FDnaLocRow> Back;
	FText Error;
	if (TestTrue(TEXT("read back"), ReadDnaLocCsv(TEXT("﻿") + WriteDnaLocCsv(Out), Back, Error)) && TestEqual(TEXT("every row"), Back.Num(), Out.Num()))
	{
		bool bSame = true;
		for (int32 Index = 0; Index < Out.Num(); ++Index)
		{
			bSame &= Back[Index].Namespace == Out[Index].Namespace && Back[Index].Key == Out[Index].Key && Back[Index].Source == Out[Index].Source && Back[Index].Translation == Out[Index].Translation;
		}
		TestTrue(TEXT("as they were"), bSame);
	}
	TestFalse(TEXT("a table without the columns is refused"), ReadDnaLocCsv(TEXT("LineId,Voice\nA,B\n"), Back, Error));

	// -- the import: into a gathered target's archive; a key not gathered, or a source rewritten since, stays out
	const FString Root = FPaths::ConvertRelativePathToFull(FPaths::AutomationTransientDir() / TEXT("DnaLocTarget_") + FGuid::NewGuid().ToString(EGuidFormats::Digits));
	ON_SCOPE_EXIT { IFileManager::Get().DeleteDirectory(*Root, false, true); };
	FDnaLocTarget Target;
	Target.Directory = Root / TEXT("Game");
	Target.Name = TEXT("Game");
	Target.NativeCulture = TEXT("zh-Hans");
	{
		FLocTextHelper Gathered(Target.Directory, TEXT("Game.manifest"), TEXT("Game.archive"), Target.NativeCulture, { TEXT("en") }, nullptr);
		Gathered.LoadManifest(ELocTextHelperLoadFlags::Create);
		for (const FDnaLocRow* Row : { Line, Choice })
		{
			Gathered.AddSourceText(FLocKey(Row->Namespace), FLocItem(Row->Source), FManifestContext(FLocKey(Row->Key)));
		}
		if (!TestTrue(TEXT("a gathered target"), Gathered.SaveManifest(&Error)))
		{
			return false;
		}
	}
	TArray<FDnaLocRow> Translated = { *Line, *Choice, *Title, *Spoken };
	Translated[0].Translation = TEXT("Is it over...?");
	Translated[1].Source = TEXT("别大意!");
	Translated[1].Translation = TEXT("Careful!");
	Translated[2].Translation = TEXT("Lost Machine");
	Translated[3].Translation.Reset();
	FDnaLocImport Import;
	if (TestTrue(TEXT("import"), ImportDnaLocTranslations(Target, TEXT("en"), Translated, Import, Error)))
	{
		TestEqual(TEXT("one imported"), Import.Imported, 1);
		TestEqual(TEXT("one without a translation"), Import.Empty, 1);
		TestEqual(TEXT("a rewritten source and a key not gathered stay out"), Import.Skipped.Num(), 2);
	}
	TArray<FDnaLocRow> Filled = { *Line, *Choice };
	TestEqual(TEXT("the archive has it"), FillDnaLocTranslations(Target, TEXT("en"), Filled), 1);
	TestEqual(TEXT("the translation"), Filled[0].Translation, FString(TEXT("Is it over...?")));
	TestTrue(TEXT("again"), ImportDnaLocTranslations(Target, TEXT("en"), Translated, Import, Error));
	TestEqual(TEXT("nothing new the second time"), Import.Unchanged, 1);
	FDnaLocTarget Missing = Target;
	Missing.Directory = Root / TEXT("Nothing");
	TestFalse(TEXT("a target never gathered is refused"), ImportDnaLocTranslations(Missing, TEXT("en"), Translated, Import, Error));
	return !HasAnyErrors();
}

#endif
