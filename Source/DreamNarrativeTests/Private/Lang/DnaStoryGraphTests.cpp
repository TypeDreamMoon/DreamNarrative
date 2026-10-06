// Copyright DreamMoon. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Fixtures/DnaTestWorld.h"
#include "Graph/DnaStoryGraph.h"
#include "Misc/FileHelper.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaStoryGraphTest, "DreamNarrative.Lang.StoryGraph", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDnaStoryGraphTest::RunTest(const FString& Parameters)
{
	using namespace UE::DreamNarrative::Tests;
	using namespace UE::DreamNarrative::Graph;

	const FString Relative = TEXT("Tests/Corpus/Validate/Samples/automata.dna");
	FDnaTestWorld World(*this);
	if (!World.CompilePluginFiles({ Relative }))
	{
		return false;
	}
	FDnaStoryGraph Graph;
	BuildDnaStoryGraph(World.Program(), Graph);

	// What leads where, each edge with its kind and condition.
	auto Has = [&Graph](EDnaGraphNodeKind FromKind, const TCHAR* From, EDnaGraphEdgeKind Kind, EDnaGraphNodeKind ToKind, const TCHAR* To, const TCHAR* Condition = TEXT(""))
	{
		const int32 A = Graph.Find(FromKind, From);
		const int32 B = Graph.Find(ToKind, To);
		return Graph.Edges.ContainsByPredicate([A, B, Kind, Condition](const FDnaGraphEdge& Edge)
		{
			return Edge.From == A && Edge.To == B && Edge.Kind == Kind && Edge.Condition == Condition;
		});
	};
	TestTrue(TEXT("an advance rule, with its condition"), Has(EDnaGraphNodeKind::Chapter, TEXT("序章"), EDnaGraphEdgeKind::Advance, EDnaGraphNodeKind::Chapter, TEXT("工厂深处"), TEXT("巨型机械已击破")));
	TestTrue(TEXT("a chapter after another"), Has(EDnaGraphNodeKind::Chapter, TEXT("序章"), EDnaGraphEdgeKind::After, EDnaGraphNodeKind::Chapter, TEXT("工厂深处")));
	TestTrue(TEXT("an event opens a dialogue under its condition"), Has(EDnaGraphNodeKind::Event, TEXT("抵达撤离点"), EDnaGraphEdgeKind::Opens, EDnaGraphNodeKind::Dialogue, TEXT("撤离_A"),
		TEXT("chapter == 工厂深处 and 路线 == A")));
	TestTrue(TEXT("an advance command"), Has(EDnaGraphNodeKind::Dialogue, TEXT("撤离_A"), EDnaGraphEdgeKind::Advance, EDnaGraphNodeKind::Chapter, TEXT("结局_A")));
	TestTrue(TEXT("a dialogue emits an event"), Has(EDnaGraphNodeKind::Dialogue, TEXT("九S_会合"), EDnaGraphEdgeKind::Emit, EDnaGraphNodeKind::Event, TEXT("作战目标")));
	TestTrue(TEXT("a quest plays a stage"), Has(EDnaGraphNodeKind::Quest, TEXT("鲭鱼结局"), EDnaGraphEdgeKind::Play, EDnaGraphNodeKind::Stage, TEXT("结局字幕_Q")));

	// Every node a file declares points at its line there.
	FString Text;
	FFileHelper::LoadFileToString(Text, *(GetPluginDir() / Relative));
	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines, /*bCullEmpty=*/false);
	int32 Declared = 0;
	for (const FDnaGraphNode& Node : Graph.Nodes)
	{
		if (Node.Line == 0)
		{
			continue;
		}
		++Declared;
		const FString Line = Lines.IsValidIndex(Node.Line - 1) ? Lines[Node.Line - 1] : FString();
		TestTrue(FString::Printf(TEXT("%s %s is declared at line %d"), LexToString(Node.Kind), *Node.Name, Node.Line),
			Line.StartsWith(FString(LexToString(Node.Kind)) + TEXT(" ") + Node.Name));
	}
	TestTrue(TEXT("most of the sample is declared"), Declared > 30);
	const int32 Report = Graph.Find(EDnaGraphNodeKind::Dialogue, TEXT("侦察_大型"));
	TestTrue(TEXT("a dialogue says what opens it"), Report != INDEX_NONE && Graph.Nodes[Report].Detail.Contains(TEXT("on 发现敌人")) && Graph.Nodes[Report].Detail.Contains(TEXT("channel 侦察")));
	return !HasAnyErrors();
}

#endif
