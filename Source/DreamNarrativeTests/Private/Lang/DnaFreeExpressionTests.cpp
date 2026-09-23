// Copyright DreamMoon. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Fixtures/DnaTestWorld.h"

// An expression that stands outside any document -- a test's `expect`, a debugger's watch -- is
// checked and compiled against the program exactly as one inside a document would be.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaFreeExpressionTest, "DreamNarrative.Lang.FreeExpression", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDnaFreeExpressionTest::RunTest(const FString& Parameters)
{
	using namespace UE::DreamNarrative::Tests;

	FDnaTestWorld World(*this);
	if (!World.CompilePluginFiles({ TEXT("Tests/Corpus/Validate/Samples/ring.dna") }))
	{
		return false;
	}

	FDnaValue Value;
	TestTrue(TEXT("arithmetic evaluates"), World.Evaluate(TEXT("换水次数 + 2"), Value) && Value.Type == EDnaFactType::Int && Value.Int == 2);
	TestTrue(TEXT("a tag member resolves against the fact"), World.Evaluate(TEXT("梅蕾状态 == 含苞"), Value) && Value.AsBool());
	TestTrue(TEXT("tag membership"), World.Evaluate(TEXT("梅蕾状态 in (含苞, 初绽)"), Value) && Value.AsBool());
	TestTrue(TEXT("a grouped fact"), World.Evaluate(TEXT("not 墨梅册.第一页"), Value) && Value.AsBool());

	// Errors come back as the diagnostic the source would have raised.
	FString Error;
	TestEqual(TEXT("an undeclared fact"), World.CompileExpression(TEXT("不存在 > 1"), true, Error), INDEX_NONE);
	TestTrue(TEXT("says DNS3001"), Error.StartsWith(TEXT("DNS3001")));
	TestEqual(TEXT("a member outside the set"), World.CompileExpression(TEXT("梅蕾状态 == 结果"), true, Error), INDEX_NONE);
	TestTrue(TEXT("says DNS4002"), Error.StartsWith(TEXT("DNS4002")));
	TestEqual(TEXT("a tag is not a condition"), World.CompileExpression(TEXT("梅蕾状态"), true, Error), INDEX_NONE);
	TestTrue(TEXT("says DNS4001"), Error.StartsWith(TEXT("DNS4001")));
	TestEqual(TEXT("a syntax error"), World.CompileExpression(TEXT("换水次数 >"), true, Error), INDEX_NONE);
	TestTrue(TEXT("says DNS2007"), Error.StartsWith(TEXT("DNS2007")));
	return !HasAnyErrors();
}

#endif
