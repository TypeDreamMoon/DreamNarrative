// Copyright DreamMoon. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Fixtures/DnaTestWorld.h"
#include "Lang/DnaEditSession.h"
#include "Lang/DnaStaging.h"
#include "Misc/FileHelper.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaStagingTest, "DreamNarrative.Lang.Staging", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDnaStagingTest::RunTest(const FString& Parameters)
{
	using namespace UE::DreamNarrative::Tests;
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
	const FString Prefix = GetDnaStagingPrefix(TEXT("击破之后"));
	auto Generated = [&Session, &Prefix]()
	{
		TArray<FString> Names;
		for (const FDnaLineNodePtr& Root : Session.GetDocument().Roots)
		{
			if (Root.IsValid() && Root->Kind == EDnaLineKind::Stage && Root->Payload.Name.StartsWith(Prefix)) { Names.Add(Root->Payload.Name); }
		}
		return Names;
	};
	auto Attached = [&Session](const FString& Dialogue)
	{
		int32 Count = 0;
		const FDnaLineNode* Root = Session.FindDeclaration(EDnaLineKind::Dialogue, Dialogue);
		TFunction<void(const FDnaLineNode&)> Visit = [&](const FDnaLineNode& Node)
		{
			for (const FDnaLineNodePtr& Child : Node.Children)
			{
				if (!Child.IsValid()) { continue; }
				Count += Child->Kind == EDnaLineKind::Speech && !Child->Payload.Modifiers.Stage.IsEmpty() ? 1 : 0;
				Visit(*Child);
			}
		};
		if (Root) { Visit(*Root); }
		return Count;
	};

	// -- the first roll: 9S and the operator, each spoken to by the other
	FText Error;
	TestTrue(TEXT("stage the dialogue after the boss"), StageDnaDialogue(Session, TEXT("击破之后"), 1, Error));
	TArray<FString> Stages = Generated();
	Stages.Sort();
	TestEqual(TEXT("one stage per speaker and listener"), FString::Join(Stages, TEXT(", ")), Prefix + TEXT("九S_对操作员, ") + Prefix + TEXT("操作员_对九S"));
	TestEqual(TEXT("every line attached"), Attached(TEXT("击破之后")), 3);
	const FString FirstRoll = Session.GetText();

	// -- a reroll replaces what the first made, and the same seed gives the same text again
	TestTrue(TEXT("roll again"), StageDnaDialogue(Session, TEXT("击破之后"), 2, Error));
	TestEqual(TEXT("still two stages"), Generated().Num(), 2);
	TestEqual(TEXT("still every line once"), Attached(TEXT("击破之后")), 3);
	TestTrue(TEXT("and back to the first seed"), StageDnaDialogue(Session, TEXT("击破之后"), 1, Error));
	TestEqual(TEXT("the same text"), Session.GetText(), FirstRoll);
	TestEqual(TEXT("three edits to undo"), [&Session]() { int32 Steps = 0; while (Session.Undo()) { ++Steps; } return Steps; }(), 3);
	TestEqual(TEXT("back to the sample"), Session.GetText(), Original);

	// -- a line with a stage of its own keeps it: the escape's one line has the flash, so nothing is left to stage
	TestFalse(TEXT("nothing to stage in the escape"), StageDnaDialogue(Session, TEXT("撤离_B"), 3, Error));
	TestEqual(TEXT("and nothing changed"), Session.GetText(), Original);
	TestTrue(TEXT("stage the dialogue after the boss once more"), StageDnaDialogue(Session, TEXT("击破之后"), 5, Error));

	// -- what came out compiles and checks
	FDnaTestWorld World(*this);
	TestTrue(TEXT("the staged sample compiles"), World.CompileText(Session.GetText(), Relative));
	return !HasAnyErrors();
}

#endif
