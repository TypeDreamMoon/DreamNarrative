// Copyright DreamMoon. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Algo/Count.h"
#include "DreamNarrativeDialogue.h"
#include "DreamNarrativeScript.h"
#include "DreamNarrativeSettings.h"
#include "Fixtures/DnaTestWorld.h"
#include "Generation/DnaGenerator.h"
#include "Lang/DnaEditSession.h"
#include "Lang/DnaIds.h"
#include "Lang/DnaProject.h"
#include "Misc/FileHelper.h"
#include "Misc/ScopeExit.h"

using namespace UE::DreamNarrative::Tests;
using namespace UE::DreamNarrative::Lang;

namespace
{
	/** Every line and choice of the scripts: id -> its text, and whether the source gave the id. */
	struct FSpoken
	{
		FString Text;
		bool bExplicit = false;
	};

	TMap<FString, FSpoken> CollectSpoken(const TArray<UDreamNarrativeScript*>& Scripts)
	{
		TMap<FString, FSpoken> Out;
		for (const UDreamNarrativeScript* Script : Scripts)
		{
			const UDnaDialogueSection* Section = Script ? Script->FindSection<UDnaDialogueSection>() : nullptr;
			if (!Section) { continue; }
			for (const FDnaDialogueNodeData& Node : Section->Nodes)
			{
				for (const FDnaLineData& Line : Node.Lines) { Out.Add(Line.Id.ToString(), { Line.Text.ToString(), Line.bExplicitId }); }
				for (const FDnaChoiceGroupData& Group : Node.ChoiceGroups)
				{
					for (const FDnaChoiceData& Option : Group.Options) { Out.Add(Option.Id.ToString(), { Option.Label.ToString(), Option.bExplicitId }); }
				}
			}
		}
		return Out;
	}

	bool Compile(FAutomationTestBase& Test, const TArray<FDnaTestSource>& Sources, TArray<UDreamNarrativeScript*>& OutScripts, TArray<FDnaLocManifest>* OutLocalization = nullptr)
	{
		FDnaGenerateOptions Options;
		Options.bSave = false;
		Options.bTransient = true;
		FDnaProgram Program;
		OutScripts.Reset();
		if (OutLocalization) { OutLocalization->Reset(); }
		return CompileDnaSources(Test, Sources, Options, Program, OutScripts, OutLocalization);
	}

	/** Every translation key of the manifests, with its source text. */
	TMap<FString, FString> CollectKeys(const TArray<FDnaLocManifest>& Manifests)
	{
		TMap<FString, FString> Out;
		for (const FDnaLocManifest& Manifest : Manifests)
		{
			for (const FDnaLocEntry& Entry : Manifest.Entries) { Out.Add(Manifest.Namespace + TEXT("/") + Entry.Key, Entry.Source); }
		}
		return Out;
	}

	const TCHAR* SmallSource = TEXT(
		"character 老周\n"
		"fact 零件: int\n"
		"stage 推近\n"
		"  base: line\n"
		"  camera\n"
		"    0%       push 30cm over 100%\n"
		"stage 字幕\n"
		"  duration 1.0s\n"
		"  ui\n"
		"    0.0s..1.0s   text 「灯亮了」\n"
		"quest 修灯\n"
		"  title: 修好灯塔\n"
		"  objective 零件 >= 3\n"
		"    show: 找齐零件\n"
		"  objective 零件 >= 5 optional\n"
		"    show: 多带几个 #备用零件\n"
		"archive 守夜日志\n"
		"  entry\n"
		"    第一天。\n"
		"  entry #第二天\n"
		"    第二天。\n"
		"dialogue 老周_初见 for 老周\n"
		"  老周: 你就是新来的?\n"
		"  老周(皱眉): 灯坏了三天了。 for 2s @推近\n"
		"  老周: 早就有 id 的一句。 #老周_开口\n"
		"  > 风从门缝里灌进来。\n"
		"  - 我去修。\n"
		"      老周: 灯室里缺三个零件。\n"
		"  - 先歇一会儿。 once\n"
		"  老周: 第一行\n"
		"    接着第二行。\n");
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaFreezeIdsTest, "DreamNarrative.Lang.FreezeIds", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDnaFreezeIdsTest::RunTest(const FString& Parameters)
{
	TArray<FDnaTestSource> Sources;
	if (!ReadPluginSources(*this, { TEXT("Tests/Corpus/Validate/Samples/automata.dna") }, Sources)) { return false; }
	Sources.Add({ TEXT("Freeze.dna"), SmallSource });

	TArray<UDreamNarrativeScript*> Before;
	TArray<FDnaLocManifest> LocBefore;
	if (!Compile(*this, Sources, Before, &LocBefore)) { return false; }
	const TMap<FString, FSpoken> SpokenBefore = CollectSpoken(Before);
	const TMap<FString, FString> KeysBefore = CollectKeys(LocBefore);
	int32 Numbered = 0;
	for (const FDnaLocManifest& Manifest : LocBefore)
	{
		Numbered += static_cast<int32>(Algo::CountIf(Manifest.Entries, [](const FDnaLocEntry& Entry) { return !Entry.bStable; }));
	}

	TMap<FString, TMap<int32, FString>> Generated;
	CollectDnaGeneratedIds(LocBefore, Generated);
	TArray<FDnaTestSource> Frozen = Sources;
	int32 Total = 0;
	for (FDnaTestSource& Source : Frozen)
	{
		FDnaEditSession Session;
		Session.Open(Source.Path, Source.Text);
		int32 Count = 0;
		FText Error;
		const TMap<int32, FString>* Ids = Generated.Find(Source.Path);
		if (!TestTrue(TEXT("freezes"), Ids && FreezeDnaIds(Session, *Ids, Count, Error))) { AddError(Error.ToString()); return false; }
		Total += Count;
		Source.Text = Session.GetText();
	}
	TestTrue(TEXT("ids were written"), Total > 0);
	TestEqual(TEXT("one id per text numbered by position"), Total, Numbered);
	TestTrue(TEXT("lines and choices among them"), Numbered > static_cast<int32>(Algo::CountIf(SpokenBefore, [](const TPair<FString, FSpoken>& Pair) { return !Pair.Value.bExplicit; })));

	TArray<UDreamNarrativeScript*> After;
	TArray<FDnaLocManifest> LocAfter;
	if (!Compile(*this, Frozen, After, &LocAfter)) { return false; }
	const TMap<FString, FSpoken> SpokenAfter = CollectSpoken(After);
	TestTrue(TEXT("every translation key and its text are as they were"), CollectKeys(LocAfter).OrderIndependentCompareEqual(KeysBefore));
	for (const FDnaLocManifest& Manifest : LocAfter)
	{
		for (const FDnaLocEntry& Entry : Manifest.Entries) { TestTrue(*FString::Printf(TEXT("%s holds still now"), *Entry.Key), Entry.bStable); }
	}
	TestTrue(TEXT("an objective's text is frozen on its show line"), Frozen.Last().Text.Contains(TEXT("show: 找齐零件 #objective1\n")));
	TestTrue(TEXT("one with an id of its own is left alone"), Frozen.Last().Text.Contains(TEXT("show: 多带几个 #备用零件\n")));
	TestTrue(TEXT("an archive entry on its entry line"), Frozen.Last().Text.Contains(TEXT("  entry #entry0\n")));
	TestTrue(TEXT("a stage's text on its line"), Frozen.Last().Text.Contains(TEXT("text 「灯亮了」 #text0\n")));
	TestEqual(TEXT("as many lines and choices as before"), SpokenAfter.Num(), SpokenBefore.Num());
	for (const TPair<FString, FSpoken>& Pair : SpokenBefore)
	{
		const FSpoken* Now = SpokenAfter.Find(Pair.Key);
		if (!TestNotNull(*FString::Printf(TEXT("%s keeps its id"), *Pair.Key), Now)) { continue; }
		TestEqual(*FString::Printf(TEXT("%s keeps its text"), *Pair.Key), Now->Text, Pair.Value.Text);
		TestTrue(*FString::Printf(TEXT("%s now carries it in the source"), *Pair.Key), Now->bExplicit);
	}
	TestTrue(TEXT("the modifiers stay where they were"), Frozen.Last().Text.Contains(TEXT("for 2s @推近 #老周_初见.2")));
	TestTrue(TEXT("a line with an id of its own is left alone"), Frozen.Last().Text.Contains(TEXT("早就有 id 的一句。 #老周_开口\n")));

	// A line added at the top moves nothing now.
	FDnaTestSource& Small = Frozen.Last();
	Small.Text = Small.Text.Replace(TEXT("dialogue 老周_初见 for 老周\n"), TEXT("dialogue 老周_初见 for 老周\n  老周: 新加的一句。\n"));
	TArray<UDreamNarrativeScript*> Edited;
	TArray<FDnaLocManifest> LocEdited;
	if (!Compile(*this, Frozen, Edited, &LocEdited)) { return false; }
	const TMap<FString, FSpoken> SpokenEdited = CollectSpoken(Edited);
	for (const TPair<FString, FSpoken>& Pair : SpokenAfter)
	{
		const FSpoken* Now = SpokenEdited.Find(Pair.Key);
		TestTrue(*FString::Printf(TEXT("%s stays put"), *Pair.Key), Now && Now->Text == Pair.Value.Text);
	}
	TMap<FString, TMap<int32, FString>> Again;
	CollectDnaGeneratedIds(LocEdited, Again);
	TestEqual(TEXT("only the new line is left to freeze"), Again.FindRef(TEXT("Freeze.dna")).Num(), 1);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaDottedIdTest, "DreamNarrative.Lang.DottedIds", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDnaDottedIdTest::RunTest(const FString& Parameters)
{
	FDnaEditSession Session;
	Session.Open(TEXT("Dotted.dna"), TEXT(
		"character 甲\n"
		"dialogue 对话 for 甲\n"
		"  甲: 一。 #对话.3\n"
		"  甲: 二。 #对话.12.b\n"
		"  - 三。 once #对话.c2\n"
		"  甲: 句号结尾的话. #普通\n"
		"  甲: 版本 1.5 不是 id\n"));
	TMap<int32, FString> Ids;
	FString LastText;
	Session.GetDocument().ForEachNode([&Ids, &LastText](const FDnaLineNode& Node)
	{
		if (!Node.Payload.Modifiers.Id.IsEmpty()) { Ids.Add(Node.LineNumber(), Node.Payload.Modifiers.Id); }
		if (Node.LineNumber() == 7) { LastText = Node.Payload.Text.Raw; }
	});
	TestEqual(TEXT("a numbered id"), Ids.FindRef(3), FString(TEXT("对话.3")));
	TestEqual(TEXT("dots on dots"), Ids.FindRef(4), FString(TEXT("对话.12.b")));
	TestEqual(TEXT("a choice id after once"), Ids.FindRef(5), FString(TEXT("对话.c2")));
	TestEqual(TEXT("a plain id"), Ids.FindRef(6), FString(TEXT("普通")));
	TestFalse(TEXT("a number in the text is no id"), Ids.Contains(7));
	TestEqual(TEXT("and stays text"), LastText, FString(TEXT("版本 1.5 不是 id")));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaRequireIdsTest, "DreamNarrative.Lang.RequireLineIds", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDnaRequireIdsTest::RunTest(const FString& Parameters)
{
	UDreamNarrativeSettings* Settings = GetMutableDefault<UDreamNarrativeSettings>();
	const bool bWas = Settings->bRequireLineIds;
	ON_SCOPE_EXIT { Settings->bRequireLineIds = bWas; };
	const FString Path = TEXT("Required.dna");
	const TMap<FString, FString> Texts = { { Path, TEXT(
		"character 甲\n"
		"dialogue 对话 for 甲\n"
		"  甲: 有 id。 #对话.1\n"
		"  甲: 没有 id。\n"
		"  - 选项。 #对话.c1\n"
		"  - 也没有。\n") } };
	auto Count = [&Path, &Texts]()
	{
		FDnaProgram Program;
		FDnaDiagnosticSink Diagnostics;
		LoadDnaProgram({ Path }, Program, Diagnostics, &Texts);
		TArray<int32> Lines;
		for (const FDnaDiagnostic& Each : Diagnostics.GetDiagnostics())
		{
			if (Each.Code == TEXT("DNS6006")) { Lines.Add(Each.Span.Line); }
		}
		return Lines;
	};
	Settings->bRequireLineIds = false;
	TestEqual(TEXT("off: nothing is required"), Count().Num(), 0);
	Settings->bRequireLineIds = true;
	TestEqual(TEXT("on: each line and choice without an id"), Count(), TArray<int32>{ 4, 6 });
	return !HasAnyErrors();
}

#endif
