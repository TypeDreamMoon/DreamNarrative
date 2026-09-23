// Copyright DreamMoon. All Rights Reserved.
//
// Unit checks on the lexical layer and the expression parser, below the corpus.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Lang/DnaExpr.h"
#include "Lang/DnaLexer.h"
#include "Lang/DnaSource.h"


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaLexerTest, "DreamNarrative.Lang.Lexer", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDnaLexerTest::RunTest(const FString& Parameters)
{
	using namespace UE::DreamNarrative::Lang;

	{
		const FDnaSourceText Source(TEXT("mem"), TEXT("换水次数 >= 5 and 梅蕾状态 == 初绽"));
		FDnaDiagnosticSink Sink;
		TArray<FDnaToken> Tokens;
		TokenizeDnaSegment(Source, 0, Source.Len(), Tokens, Sink);
		TestEqual(TEXT("no diagnostics"), Sink.Num(), 0);
		TestEqual(TEXT("token count"), Tokens.Num(), 8);
		TestTrue(TEXT("cjk identifier"), Tokens[0].Is(EDnaTokenKind::Identifier) && Tokens[0].Text == TEXT("换水次数"));
		TestTrue(TEXT("full-width >="), Tokens[1].Is(EDnaTokenKind::GreaterEqual));
		TestTrue(TEXT("int"), Tokens[2].Is(EDnaTokenKind::Int) && Tokens[2].Integer == 5);
		TestTrue(TEXT("and keyword"), Tokens[3].IsKeyword(EDnaKeyword::And));
		TestTrue(TEXT("full-width =="), Tokens[5].Is(EDnaTokenKind::EqualEqual));
	}
	{
		const FDnaSourceText Source(TEXT("mem"), TEXT("0.2s 500ms 20m 2h 30% 1.9s..2.1s"));
		FDnaDiagnosticSink Sink;
		TArray<FDnaToken> Tokens;
		TokenizeDnaSegment(Source, 0, Source.Len(), Tokens, Sink);
		TestEqual(TEXT("no diagnostics"), Sink.Num(), 0);
		TestTrue(TEXT("0.2s"), Tokens[0].Is(EDnaTokenKind::Duration) && FMath::IsNearlyEqual(Tokens[0].Real, 0.2));
		TestTrue(TEXT("500ms"), Tokens[1].Is(EDnaTokenKind::Duration) && FMath::IsNearlyEqual(Tokens[1].Real, 0.5));
		TestTrue(TEXT("20m"), Tokens[2].Is(EDnaTokenKind::Duration) && FMath::IsNearlyEqual(Tokens[2].Real, 1200.0));
		TestTrue(TEXT("2h"), Tokens[3].Is(EDnaTokenKind::Duration) && FMath::IsNearlyEqual(Tokens[3].Real, 7200.0));
		TestTrue(TEXT("30%"), Tokens[4].Is(EDnaTokenKind::Percent) && FMath::IsNearlyEqual(Tokens[4].Real, 30.0));
		TestTrue(TEXT("range start"), Tokens[5].Is(EDnaTokenKind::Duration));
		TestTrue(TEXT("dotdot"), Tokens[6].Is(EDnaTokenKind::DotDot));
		TestTrue(TEXT("range end"), Tokens[7].Is(EDnaTokenKind::Duration));
	}
	{
		const FDnaSourceText Source(TEXT("mem"), TEXT("凛: 她说 // 注释\n「引号//不是注释」 // 真注释"));
		FDnaScanResult Scan;
		FDnaDiagnosticSink Sink;
		ScanDnaLines(Source, Scan, Sink);
		TestEqual(TEXT("two lines"), Scan.Lines.Num(), 2);
		TestEqual(TEXT("content before comment"), Scan.Lines[0].Content, FString(TEXT("凛: 她说")));
		TestEqual(TEXT("comment kept"), Scan.Lines[0].Comment, FString(TEXT("// 注释")));
		TestEqual(TEXT("quoted slashes are text"), Scan.Lines[1].Content, FString(TEXT("「引号//不是注释」")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaExprTest, "DreamNarrative.Lang.Expr", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDnaExprTest::RunTest(const FString& Parameters)
{
	using namespace UE::DreamNarrative::Lang;

	auto Parse = [this](const TCHAR* Text) -> FString
	{
		const FDnaSourceText Source(TEXT("mem"), Text);
		FDnaDiagnosticSink Sink;
		FDnaExprPtr Expr = ParseDnaExpressionSegment(Source, 0, Source.Len(), Sink);
		if (!Expr.IsValid())
		{
			return Sink.Num() > 0 ? FDnaDiagnosticSink::ToWireString(Sink.GetDiagnostics()[0]) : TEXT("<null>");
		}
		return Expr->ToString();
	};

	TestEqual(TEXT("precedence"), Parse(TEXT("a or b and not c == 1 + 2 * 3")), FString(TEXT("(a or (b and (not (c == (1 + (2 * 3))))))")));
	TestEqual(TEXT("modulo"), Parse(TEXT("周期 % 3 == 0")), FString(TEXT("((周期 % 3) == 0)")));
	TestEqual(TEXT("dotted and call"), Parse(TEXT("猎狼.completed and visited(凛_日常) == 0")), FString(TEXT("(猎狼.completed and (visited(凛_日常) == 0))")));
	TestEqual(TEXT("in list"), Parse(TEXT("梅蕾状态 in (初绽, 盛开)")), FString(TEXT("(梅蕾状态 in (初绽, 盛开))")));
	TestEqual(TEXT("chapter keyword"), Parse(TEXT("chapter == 第一章")), FString(TEXT("(chapter == 第一章)")));
	TestEqual(TEXT("negative literal folds"), Parse(TEXT("x > -3")), FString(TEXT("(x > -3)")));
	TestEqual(TEXT("percent literal"), Parse(TEXT("chance(30%)")), FString(TEXT("chance(0.3)")));
	TestEqual(TEXT("missing operand"), Parse(TEXT("a >")), FString(TEXT("DNS2007: Expected a value, found end of line")));
	TestEqual(TEXT("trailing"), Parse(TEXT("1 2")), FString(TEXT("DNS2008: Unexpected '2' after the expression")));
	return true;
}

#endif
