// Copyright DreamMoon. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Fixtures/DnaTestWorld.h"
#include "Lang/DnaEditSession.h"
#include "Lang/DnaStageCompiler.h"
#include "Misc/FileHelper.h"
#include "Replay/DnaReplay.h"

namespace UE::DreamNarrative::Tests::EditSessionTests
{
	/** The lines a change removed and added, `- ...` and `+ ...`, in order. */
	TArray<FString> Changed(const FString& Before, const FString& After)
	{
		return Replay::DiffLines(Before, After).FilterByPredicate([](const FString& Line) { return Line.StartsWith(TEXT("- ")) || Line.StartsWith(TEXT("+ ")); });
	}

	/** The 1-based number of the first line of Text containing Needle; 0 when none does. */
	int32 LineWith(const FString& Text, const FString& Needle)
	{
		TArray<FString> Lines;
		Text.ParseIntoArrayLines(Lines, /*bCullEmpty=*/false);
		for (int32 Index = 0; Index < Lines.Num(); ++Index)
		{
			if (Lines[Index].Contains(Needle)) { return Index + 1; }
		}
		return 0;
	}

	/** A stage of the session's text, compiled; false when it is not there. */
	bool CompileStage(const UE::DreamNarrative::Lang::FDnaEditSession& Session, const FString& Name, FDnaStageData& Out, int32& OutErrors)
	{
		using namespace UE::DreamNarrative::Lang;
		for (const FDnaLineNodePtr& Root : Session.GetDocument().Roots)
		{
			if (Root.IsValid() && Root->Kind == EDnaLineKind::Stage && Root->Payload.Name == Name)
			{
				FDnaDiagnosticSink Sink;
				CompileDnaStage(*Root, Out, Sink);
				OutErrors = Sink.NumErrors();
				return true;
			}
		}
		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaEditSessionTest, "DreamNarrative.Lang.EditSession", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDnaEditSessionTest::RunTest(const FString& Parameters)
{
	using namespace UE::DreamNarrative::Tests;
	using namespace UE::DreamNarrative::Tests::EditSessionTests;
	using namespace UE::DreamNarrative::Lang;

	const FString Relative = TEXT("Tests/Corpus/Validate/Samples/automata.dna");
	FString Original;
	if (!FFileHelper::LoadFileToString(Original, *(GetPluginDir() / Relative)))
	{
		AddError(TEXT("cannot read the sample"));
		return false;
	}
	FDnaEditSession Session;
	Session.Open(Relative, Original);
	FText Error;

	// -- a condition changed: one header line, comment and the rest of the header kept
	FString Before = Session.GetText();
	TestTrue(TEXT("change a condition"), Session.SetDialogueCondition(TEXT("侦察_大型"), TEXT("发现敌人.类型 == 大型 and not 交战中"), Error));
	TestEqual(TEXT("one line out, one in"), FString::Join(Changed(Before, Session.GetText()), TEXT("\n")), FString(TEXT(
		"- dialogue 侦察_大型 on 发现敌人 if 发现敌人.类型 == 大型 channel 侦察 priority 3 cooldown 20s\n"
		"+ dialogue 侦察_大型 on 发现敌人 if 发现敌人.类型 == 大型 and not 交战中 channel 侦察 priority 3 cooldown 20s")));

	// -- dropped, then given one where there was none
	Before = Session.GetText();
	TestTrue(TEXT("drop a condition"), Session.SetDialogueCondition(TEXT("九S_会合"), FString(), Error));
	TestEqual(TEXT("the header without it"), FString::Join(Changed(Before, Session.GetText()), TEXT("\n")), FString(TEXT(
		"- dialogue 九S_会合 for 九S if not 已会合\n"
		"+ dialogue 九S_会合 for 九S")));
	Before = Session.GetText();
	TestTrue(TEXT("add a condition"), Session.SetDialogueCondition(TEXT("九S_日常"), TEXT("已会合"), Error));
	TestEqual(TEXT("the header with it"), FString::Join(Changed(Before, Session.GetText()), TEXT("\n")), FString(TEXT(
		"- dialogue 九S_日常 for 九S\n"
		"+ dialogue 九S_日常 for 九S if 已会合")));

	// -- not a condition: refused, nothing changed
	Before = Session.GetText();
	TestFalse(TEXT("a broken condition is refused"), Session.SetDialogueCondition(TEXT("九S_日常"), TEXT("已会合 and"), Error));
	TestEqual(TEXT("and the text is as it was"), Session.GetText(), Before);

	// -- a new dialogue: only its lines
	Before = Session.GetText();
	TestTrue(TEXT("add a dialogue"), Session.AddDialogue(TEXT("九S_告别"), TEXT("for 九S if chapter == 结局_A"), { TEXT("九S: 下次见。") }, Error));
	TestEqual(TEXT("its lines added at the end, after a blank line"), Session.GetText(),
		Before + TEXT("\ndialogue 九S_告别 for 九S if chapter == 结局_A\n  九S: 下次见。\n"));
	TestFalse(TEXT("a name taken is refused"), Session.AddDialogue(TEXT("九S_告别"), TEXT("for 九S"), {}, Error));

	// -- connected: one `->` line at the end of the body
	Before = Session.GetText();
	TestTrue(TEXT("connect two dialogues"), Session.AddGoTo(TEXT("九S_日常"), TEXT("九S_告别"), Error));
	TestEqual(TEXT("one line added"), FString::Join(Changed(Before, Session.GetText()), TEXT("\n")), FString(TEXT("+   -> 九S_告别")));

	// -- removed: its lines, and a blank line so none doubles up
	Before = Session.GetText();
	TestTrue(TEXT("remove a dialogue"), Session.RemoveDeclaration(EDnaLineKind::Dialogue, TEXT("闲聊_发呆"), Error));
	const TArray<FString> Removed = Changed(Before, Session.GetText());
	TestTrue(TEXT("only lines went"), !Removed.ContainsByPredicate([](const FString& Line) { return Line.StartsWith(TEXT("+ ")); }));
	TestTrue(TEXT("its header among them"), Removed.Contains(TEXT("- dialogue 闲聊_发呆 on 闲置 if 已会合 and not 交战中 channel 闲聊 cooldown 45s resume")));

	// -- undo and redo walk the texts back and forth
	const FString Edited = Session.GetText();
	int32 Steps = 0;
	while (Session.Undo()) { ++Steps; }
	TestEqual(TEXT("six steps back"), Steps, 6);
	TestEqual(TEXT("to the text it started with"), Session.GetText(), Original);
	while (Session.Redo()) {}
	TestEqual(TEXT("and forward again"), Session.GetText(), Edited);

	// -- what came out still compiles (the removed dialogue was not needed by anything)
	FDnaTestWorld World(*this);
	TestTrue(TEXT("the edited sample compiles"), World.CompileText(Session.GetText(), Relative));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaStageEditTest, "DreamNarrative.Lang.StageEdit", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDnaStageEditTest::RunTest(const FString& Parameters)
{
	using namespace UE::DreamNarrative::Tests;
	using namespace UE::DreamNarrative::Tests::EditSessionTests;
	using namespace UE::DreamNarrative::Lang;

	const FString Relative = TEXT("Tests/Corpus/Validate/Samples/automata.dna");
	FString Original;
	if (!FFileHelper::LoadFileToString(Original, *(GetPluginDir() / Relative)))
	{
		AddError(TEXT("cannot read the sample"));
		return false;
	}
	FDnaEditSession Session;
	Session.Open(Relative, Original);
	FText Error;
	auto One = [this](const TArray<FString>& Lines, const TCHAR* Added)
	{
		TestEqual(TEXT("one line out, one in"), Lines.Num(), 2);
		TestTrue(*FString::Printf(TEXT("and it reads `%s`"), Added), Lines.Num() == 2 && Lines[1].Contains(Added));
	};

	// -- an entry dragged: its line, and only its time on it
	FString Before = Session.GetText();
	TestTrue(TEXT("move the effect"), Session.SetStageEntryTime(LineWith(Before, TEXT("spawn /Game/FX")), 2.5, 2.5, Error));
	One(Changed(Before, Session.GetText()), TEXT("    2.5s     spawn /Game/FX/NS_处决_斩击 at 巨型机械"));

	// a longer time keeps the verb's column, the comment at the end of the line stays
	Before = Session.GetText();
	TestTrue(TEXT("move a cut"), Session.SetStageEntryTime(LineWith(Before, TEXT("cut -> 侧面")), 10.0, 10.0, Error));
	One(Changed(Before, Session.GetText()), TEXT("    10.0s    cut -> 侧面                     // 0.9..1.1"));
	Session.Undo();

	// the end of a range
	Before = Session.GetText();
	TestTrue(TEXT("shorten a range"), Session.SetStageEntryTime(LineWith(Before, TEXT("damage window")), 0.0, 2.35, Error));
	One(Changed(Before, Session.GetText()), TEXT("    0.0s..2.35s  damage window"));

	// -- a span: the blend, the push
	Before = Session.GetText();
	TestTrue(TEXT("a longer blend"), Session.SetStageEntrySpan(LineWith(Before, TEXT("blend from game")), 0.4, Error));
	One(Changed(Before, Session.GetText()), TEXT("blend from game 0.4s"));
	Before = Session.GetText();
	TestTrue(TEXT("a longer push"), Session.SetStageEntrySpan(LineWith(Before, TEXT("push 60cm")), 1.5, Error));
	One(Changed(Before, Session.GetText()), TEXT("push 60cm over 1.5s"));

	// -- an entry added in its track, lined up with the others, and removed again
	Before = Session.GetText();
	TestTrue(TEXT("add an entry"), Session.AddStageEntry(TEXT("处决_巨型机械"), TEXT("game"), 3.0, 3.0, false, TEXT("damage window"), Error));
	TArray<FString> Lines = Changed(Before, Session.GetText());
	TestEqual(TEXT("one line in"), Lines.Num(), 1);
	TestTrue(TEXT("under the game track, its verb in the track's column"), Lines.Num() == 1 && Lines[0].Contains(TEXT("    3.0s         damage window")));
	TestTrue(TEXT("remove it"), Session.RemoveStageEntry(LineWith(Session.GetText(), TEXT("3.0s         damage window")), Error));
	TestEqual(TEXT("as it was"), Session.GetText(), Before);

	// -- a track's last entry takes the track with it
	Before = Session.GetText();
	TestTrue(TEXT("remove the only text"), Session.RemoveStageEntry(LineWith(Before, TEXT("text 「这里也有")), Error));
	TestEqual(TEXT("two lines out"), Changed(Before, Session.GetText()).Num(), 2);
	Session.Undo();

	// -- the time base switched: seconds against a three-second line, and back
	Before = Session.GetText();
	FDnaStageData Flash;
	FDnaStageData FlashBefore;
	int32 Errors = 0;
	CompileStage(Session, TEXT("骇入闪回"), FlashBefore, Errors);
	TestTrue(TEXT("to seconds"), Session.SetStageTimeBase(TEXT("骇入闪回"), false, 3.0, Error));
	if (TestTrue(TEXT("the stage is still there"), CompileStage(Session, TEXT("骇入闪回"), Flash, Errors)))
	{
		TestEqual(TEXT("and checks"), Errors, 0);
		TestFalse(TEXT("in seconds"), Flash.bLineRelative);
		TestEqual(TEXT("three of them"), Flash.Duration, 3.f);
		TestEqual(TEXT("10% of the line is 0.3 s"), Flash.Entries.Num() > 0 ? Flash.Entries[0].Start : -1.f, 0.3f, 1e-4f);
	}
	TestTrue(TEXT("and back"), Session.SetStageTimeBase(TEXT("骇入闪回"), true, 3.0, Error));
	TestEqual(TEXT("as many lines as it had"), Changed(Before, Session.GetText()).Num() % 2, 0);
	if (TestTrue(TEXT("compiles again"), CompileStage(Session, TEXT("骇入闪回"), Flash, Errors)))
	{
		TestTrue(TEXT("following its line again"), Flash.bLineRelative);
		bool bSame = Flash.Entries.Num() == FlashBefore.Entries.Num();
		for (int32 Index = 0; bSame && Index < Flash.Entries.Num(); ++Index)
		{
			bSame = FMath::IsNearlyEqual(Flash.Entries[Index].Start, FlashBefore.Entries[Index].Start, 1e-4f)
				&& FMath::IsNearlyEqual(Flash.Entries[Index].End, FlashBefore.Entries[Index].End, 1e-4f);
		}
		TestTrue(TEXT("with the times it had"), bSame);
	}

	// -- what came out still compiles
	FDnaTestWorld World(*this);
	TestTrue(TEXT("the edited sample compiles"), World.CompileText(Session.GetText(), Relative));
	return !HasAnyErrors();
}

#endif
