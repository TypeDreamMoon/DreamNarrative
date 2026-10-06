// Copyright DreamMoon. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Fixtures/DnaTestWorld.h"
#include "Lang/DnaEditSession.h"
#include "Misc/FileHelper.h"
#include "Replay/DnaReplay.h"

namespace UE::DreamNarrative::Tests::EditSessionTests
{
	/** The lines a change removed and added, `- ...` and `+ ...`, in order. */
	TArray<FString> Changed(const FString& Before, const FString& After)
	{
		return Replay::DiffLines(Before, After).FilterByPredicate([](const FString& Line) { return Line.StartsWith(TEXT("- ")) || Line.StartsWith(TEXT("+ ")); });
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

#endif
