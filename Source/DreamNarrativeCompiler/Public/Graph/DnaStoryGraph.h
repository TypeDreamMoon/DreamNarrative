// Copyright DreamMoon. All Rights Reserved.
//
// The story as a graph, read off a compiled program: chapters, quests, dialogues, events, stages and
// archives as nodes; what leads where as edges -- a chapter after another, an advance rule or command,
// a jump between dialogues, a quest taken on or finished, an event raised and the dialogues it opens,
// a stage played. Every node and edge knows the file and line it was written at, so a view can take
// the reader to the source. Nothing here is editor-only: a test reads the same graph a panel shows.

#pragma once

#include "CoreMinimal.h"
#include "Lang/DnaModel.h"

namespace UE::DreamNarrative::Graph
{
	enum class EDnaGraphNodeKind : uint8
	{
		Chapter,
		Quest,
		Dialogue,
		Event,
		Stage,
		Archive,
	};

	enum class EDnaGraphEdgeKind : uint8
	{
		/** `chapter B after A`: A -> B. */
		After,
		/** An advance rule, or an `advance` command: the chapter or dialogue it runs in -> the chapter. */
		Advance,
		/** `-> 节点` in a dialogue. */
		GoTo,
		/** `accept` / `complete` / `fail` in a dialogue or a quest's `on` block. */
		Accept,
		Complete,
		Fail,
		/** `subquest 任务` in a quest. */
		Subquest,
		/** `emit 事件` in a dialogue or a quest's `on` block. */
		Emit,
		/** `dialogue X on 事件`: the event -> the dialogue it opens. */
		Opens,
		/** `play 演出` or a line's `@演出`. */
		Play,
	};

	DREAMNARRATIVECOMPILER_API const TCHAR* LexToString(EDnaGraphNodeKind Kind);
	DREAMNARRATIVECOMPILER_API const TCHAR* LexToString(EDnaGraphEdgeKind Kind);

	struct FDnaGraphNode
	{
		EDnaGraphNodeKind Kind = EDnaGraphNodeKind::Dialogue;
		FString Name;
		/** What the header says besides the name: `for 九S if 已会合`, `on 发现敌人 · channel 侦察`, `priority: main`. */
		FString Detail;
		FString File;
		/** 1-based; 0 for a node nothing declares (an event only emitted). */
		int32 Line = 0;
	};

	struct FDnaGraphEdge
	{
		int32 From = INDEX_NONE;
		int32 To = INDEX_NONE;
		EDnaGraphEdgeKind Kind = EDnaGraphEdgeKind::GoTo;
		/** The condition it is taken under, as written; empty for none. */
		FString Condition;
		FString File;
		int32 Line = 0;
	};

	struct DREAMNARRATIVECOMPILER_API FDnaStoryGraph
	{
		TArray<FDnaGraphNode> Nodes;
		TArray<FDnaGraphEdge> Edges;

		/** The node of a kind and name; INDEX_NONE when there is none. */
		int32 Find(EDnaGraphNodeKind Kind, const FString& Name) const;

		/** The edges leaving (or entering) a node, in the order they were written. */
		TArray<const FDnaGraphEdge*> EdgesFrom(int32 Node) const;
		TArray<const FDnaGraphEdge*> EdgesTo(int32 Node) const;
	};

	/** Reads the graph off a program its documents were parsed into. */
	DREAMNARRATIVECOMPILER_API void BuildDnaStoryGraph(const Lang::FDnaProgram& Program, FDnaStoryGraph& Out);
}
