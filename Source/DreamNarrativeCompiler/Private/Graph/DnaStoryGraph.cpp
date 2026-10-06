// Copyright DreamMoon. All Rights Reserved.

#include "Graph/DnaStoryGraph.h"
#include "Lang/DnaPrinter.h"
#include "Lang/DnaSyntax.h"

namespace UE::DreamNarrative::Graph
{
	using namespace UE::DreamNarrative::Lang;

	const TCHAR* LexToString(EDnaGraphNodeKind Kind)
	{
		switch (Kind)
		{
		case EDnaGraphNodeKind::Chapter: return TEXT("chapter");
		case EDnaGraphNodeKind::Quest: return TEXT("quest");
		case EDnaGraphNodeKind::Dialogue: return TEXT("dialogue");
		case EDnaGraphNodeKind::Event: return TEXT("event");
		case EDnaGraphNodeKind::Stage: return TEXT("stage");
		case EDnaGraphNodeKind::Archive: return TEXT("archive");
		}
		return TEXT("?");
	}

	const TCHAR* LexToString(EDnaGraphEdgeKind Kind)
	{
		switch (Kind)
		{
		case EDnaGraphEdgeKind::After: return TEXT("after");
		case EDnaGraphEdgeKind::Advance: return TEXT("advance");
		case EDnaGraphEdgeKind::GoTo: return TEXT("->");
		case EDnaGraphEdgeKind::Accept: return TEXT("accept");
		case EDnaGraphEdgeKind::Complete: return TEXT("complete");
		case EDnaGraphEdgeKind::Fail: return TEXT("fail");
		case EDnaGraphEdgeKind::Subquest: return TEXT("subquest");
		case EDnaGraphEdgeKind::Emit: return TEXT("emit");
		case EDnaGraphEdgeKind::Opens: return TEXT("opens");
		case EDnaGraphEdgeKind::Play: return TEXT("play");
		}
		return TEXT("?");
	}

	int32 FDnaStoryGraph::Find(EDnaGraphNodeKind Kind, const FString& Name) const
	{
		return Nodes.IndexOfByPredicate([Kind, &Name](const FDnaGraphNode& Node) { return Node.Kind == Kind && Node.Name == Name; });
	}

	TArray<const FDnaGraphEdge*> FDnaStoryGraph::EdgesFrom(int32 Node) const
	{
		TArray<const FDnaGraphEdge*> Out;
		for (const FDnaGraphEdge& Edge : Edges) { if (Edge.From == Node) { Out.Add(&Edge); } }
		return Out;
	}

	TArray<const FDnaGraphEdge*> FDnaStoryGraph::EdgesTo(int32 Node) const
	{
		TArray<const FDnaGraphEdge*> Out;
		for (const FDnaGraphEdge& Edge : Edges) { if (Edge.To == Node) { Out.Add(&Edge); } }
		return Out;
	}

	namespace
	{
		struct FBuilder
		{
			const FDnaProgram& Program;
			FDnaStoryGraph& Graph;

			FString FileOf(const FDnaDocument* Document) const { return Document ? Document->Path : FString(); }

			int32 Node(EDnaGraphNodeKind Kind, const FString& Name, const FString& Detail = FString(), const FString& File = FString(), int32 Line = 0)
			{
				const int32 Found = Graph.Find(Kind, Name);
				if (Found != INDEX_NONE)
				{
					FDnaGraphNode& Existing = Graph.Nodes[Found];
					if (Existing.Line == 0 && Line > 0)
					{
						Existing.Detail = Detail;
						Existing.File = File;
						Existing.Line = Line;
					}
					return Found;
				}
				FDnaGraphNode& Added = Graph.Nodes.AddDefaulted_GetRef();
				Added.Kind = Kind;
				Added.Name = Name;
				Added.Detail = Detail;
				Added.File = File;
				Added.Line = Line;
				return Graph.Nodes.Num() - 1;
			}

			void Edge(int32 From, int32 To, EDnaGraphEdgeKind Kind, const FString& File, int32 Line, const FDnaExprPtr& Condition = nullptr)
			{
				FDnaGraphEdge& Added = Graph.Edges.AddDefaulted_GetRef();
				Added.From = From;
				Added.To = To;
				Added.Kind = Kind;
				Added.File = File;
				Added.Line = Line;
				Added.Condition = Condition.IsValid() ? FormatDnaExpr(*Condition) : FString();
			}

			/** The commands of a block, and every block under it: what leads out of it. */
			void Commands(const FDnaLineNode& Block, int32 From, const FString& File)
			{
				for (const FDnaLineNodePtr& ChildPtr : Block.Children)
				{
					if (!ChildPtr.IsValid() || ChildPtr->IsFloating())
					{
						continue;
					}
					const FDnaLineNode& Line = *ChildPtr;
					const FDnaLinePayload& P = Line.Payload;
					const int32 At = Line.LineNumber();
					if (Line.Kind == EDnaLineKind::Jump && !P.Target.IsEmpty() && P.Target != TEXT("end"))
					{
						Edge(From, Node(EDnaGraphNodeKind::Dialogue, P.Target), EDnaGraphEdgeKind::GoTo, File, At);
					}
					else if (Line.Kind == EDnaLineKind::Command)
					{
						switch (P.Command)
						{
						case EDnaCommandKind::Advance: Edge(From, Node(EDnaGraphNodeKind::Chapter, P.Name), EDnaGraphEdgeKind::Advance, File, At); break;
						case EDnaCommandKind::Accept: Edge(From, Node(EDnaGraphNodeKind::Quest, P.Name), EDnaGraphEdgeKind::Accept, File, At); break;
						case EDnaCommandKind::Complete: Edge(From, Node(EDnaGraphNodeKind::Quest, P.Name), EDnaGraphEdgeKind::Complete, File, At); break;
						case EDnaCommandKind::Fail: Edge(From, Node(EDnaGraphNodeKind::Quest, P.Name), EDnaGraphEdgeKind::Fail, File, At); break;
						case EDnaCommandKind::Emit: Edge(From, Node(EDnaGraphNodeKind::Event, P.Name), EDnaGraphEdgeKind::Emit, File, At); break;
						case EDnaCommandKind::Play: Edge(From, Node(EDnaGraphNodeKind::Stage, P.Name), EDnaGraphEdgeKind::Play, File, At); break;
						default: break;
						}
					}
					else if ((Line.Kind == EDnaLineKind::Speech || Line.Kind == EDnaLineKind::Narration) && !P.Modifiers.Stage.IsEmpty())
					{
						Edge(From, Node(EDnaGraphNodeKind::Stage, P.Modifiers.Stage), EDnaGraphEdgeKind::Play, File, At);
					}
					Commands(Line, From, File);
				}
			}

			void Build()
			{
				// -- declared nodes first, in a stable order, each where it is written
				for (const FString& Name : Program.ChapterOrder)
				{
					const FDnaChapterInfo& Info = Program.Chapters.FindChecked(Name);
					Node(EDnaGraphNodeKind::Chapter, Name, Info.Title.IsEmpty() ? FString() : TEXT("title: ") + Info.Title,
						FileOf(Info.Document), Info.Node ? Info.Node->LineNumber() : 0);
				}
				for (const TSharedPtr<FDnaDocument>& Document : Program.Documents)
				{
					if (!Document.IsValid())
					{
						continue;
					}
					const FString File = Document->Path;
					for (const FDnaLineNodePtr& RootPtr : Document->Roots)
					{
						if (!RootPtr.IsValid() || RootPtr->IsFloating())
						{
							continue;
						}
						const FDnaLineNode& Root = *RootPtr;
						const FDnaLinePayload& P = Root.Payload;
						switch (Root.Kind)
						{
						case EDnaLineKind::Quest:
						{
							FString Priority;
							TArray<FDnaLineNode*> Lines;
							Root.GetContentChildren(Lines);
							for (const FDnaLineNode* Line : Lines)
							{
								if (Line->Kind == EDnaLineKind::Property && Line->Payload.Name == TEXT("priority")) { Priority = TEXT("priority: ") + Line->Payload.Value; }
							}
							Node(EDnaGraphNodeKind::Quest, P.Name, Priority, File, Root.LineNumber());
							break;
						}
						case EDnaLineKind::Dialogue:
						{
							TArray<FString> Parts;
							if (!P.ForCharacter.IsEmpty()) { Parts.Add(TEXT("for ") + P.ForCharacter); }
							if (!P.OnEvent.IsEmpty()) { Parts.Add(TEXT("on ") + P.OnEvent); }
							if (P.Condition.IsValid()) { Parts.Add(TEXT("if ") + FormatDnaExpr(*P.Condition)); }
							if (!P.Channel.IsEmpty()) { Parts.Add(TEXT("channel ") + P.Channel); }
							Node(EDnaGraphNodeKind::Dialogue, P.Name, FString::Join(Parts, TEXT(" · ")), File, Root.LineNumber());
							break;
						}
						case EDnaLineKind::Event:
							Node(EDnaGraphNodeKind::Event, P.Name, P.GameplayTag.IsEmpty() ? FString() : TEXT("gameplay ") + P.GameplayTag, File, Root.LineNumber());
							break;
						case EDnaLineKind::Stage:
							Node(EDnaGraphNodeKind::Stage, P.Name, FString(), File, Root.LineNumber());
							break;
						case EDnaLineKind::Archive:
							Node(EDnaGraphNodeKind::Archive, P.Name, P.bProfile ? TEXT("profile") : FString(), File, Root.LineNumber());
							break;
						default:
							break;
						}
					}
				}

				// -- then what leads where
				for (const TSharedPtr<FDnaDocument>& Document : Program.Documents)
				{
					if (!Document.IsValid())
					{
						continue;
					}
					const FString File = Document->Path;
					for (const FDnaLineNodePtr& RootPtr : Document->Roots)
					{
						if (!RootPtr.IsValid() || RootPtr->IsFloating())
						{
							continue;
						}
						const FDnaLineNode& Root = *RootPtr;
						const FDnaLinePayload& P = Root.Payload;
						switch (Root.Kind)
						{
						case EDnaLineKind::Chapter:
						{
							const int32 Self = Node(EDnaGraphNodeKind::Chapter, P.Name);
							if (!P.Target.IsEmpty())
							{
								Edge(Node(EDnaGraphNodeKind::Chapter, P.Target), Self, EDnaGraphEdgeKind::After, File, Root.LineNumber());
							}
							TArray<FDnaLineNode*> Lines;
							Root.GetContentChildren(Lines);
							for (const FDnaLineNode* Line : Lines)
							{
								if (Line->Kind == EDnaLineKind::Advance)
								{
									Edge(Self, Node(EDnaGraphNodeKind::Chapter, Line->Payload.Target), EDnaGraphEdgeKind::Advance, File, Line->LineNumber(), Line->Payload.Condition);
								}
							}
							break;
						}
						case EDnaLineKind::Quest:
						{
							const int32 Self = Node(EDnaGraphNodeKind::Quest, P.Name);
							TArray<FDnaLineNode*> Lines;
							Root.GetContentChildren(Lines);
							for (const FDnaLineNode* Line : Lines)
							{
								if (Line->Kind == EDnaLineKind::Subquest)
								{
									Edge(Self, Node(EDnaGraphNodeKind::Quest, Line->Payload.Name), EDnaGraphEdgeKind::Subquest, File, Line->LineNumber());
								}
								else if (Line->Kind == EDnaLineKind::On)
								{
									Commands(*Line, Self, File);
								}
							}
							break;
						}
						case EDnaLineKind::Dialogue:
						{
							const int32 Self = Node(EDnaGraphNodeKind::Dialogue, P.Name);
							if (!P.OnEvent.IsEmpty())
							{
								Edge(Node(EDnaGraphNodeKind::Event, P.OnEvent), Self, EDnaGraphEdgeKind::Opens, File, Root.LineNumber(), P.Condition);
							}
							Commands(Root, Self, File);
							break;
						}
						default:
							break;
						}
					}
				}
			}
		};
	}

	void BuildDnaStoryGraph(const FDnaProgram& Program, FDnaStoryGraph& Out)
	{
		Out = FDnaStoryGraph();
		FBuilder Builder{ Program, Out };
		Builder.Build();
	}
}
