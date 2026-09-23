// Copyright DreamMoon. All Rights Reserved.
//
// The concrete syntax tree. Because the grammar is line-oriented, the tree IS the lines: every node
// is one physical line (its indentation, its content, its comment, all kept verbatim) plus the
// parsed meaning of that line, and children are the lines indented under it. Blank and comment-only
// lines are nodes too. The printer re-emits an unedited node from its raw line and only renders the
// nodes an editor changed, which is what keeps write-back format-preserving.

#pragma once

#include "CoreMinimal.h"
#include "DreamNarrativeTypes.h"
#include "Lang/DnaExpr.h"
#include "Lang/DnaLexer.h"

namespace UE::DreamNarrative::Lang
{
	enum class EDnaLineKind : uint8
	{
		Blank,
		CommentOnly,

		// top-level declarations
		File,
		Character,
		/** `fact 名: type ...` at top level, or `名: type ...` inside a group (bInGroup). */
		Fact,
		/** `fact 组名 [修饰]` opening a block of Fact lines. */
		FactGroup,
		Chapter,
		Quest,
		Dialogue,
		Stage,

		/** `key: value`. Name is the key, Value the raw text, Text the parsed text. */
		Property,
		/** A bare keyword that is a flag: `track`. Name is the keyword. */
		Flag,

		// chapter
		Implied,
		/** `fact = expr` inside `implied`. */
		Implication,
		/** `advance -> 目标 if 条件`. */
		Advance,

		// quest
		Available,
		FailIf,
		Journal,
		Objective,
		/** `on accept|complete|fail|timeout`. */
		On,
		Subquest,
		/** `reward 类别 名 [x数量]` or `reward 类别 数量`. */
		Reward,

		// dialogue
		If,
		ElseIf,
		Else,
		Choice,
		Jump,
		Speech,
		Narration,
		/** A deeper text line under a Speech or Narration: the same line continued. */
		Continuation,
		Command,

		// stage
		Duration,
		/** `camera`, `light`, ... opening a block of entries. Name is the track. */
		Track,
		Shots,
		/** `名 args...` under `shots`. */
		Shot,
		Restore,
		/** `轨道 动词 args...` under `restore`. */
		RestoreEntry,
		/** `时间[..时间] 动词 args...` under a track. */
		StageEntry,

		// test
		/** `test 名` opening a block of steps. */
		Test,
		/** `explore 名` opening a block of steps (the start of the exploration) and properties. */
		Explore,
		/** One step of a test or an explore block, one expectation under its `talk` / `start`, or one
		 *  explore property. Payload.TestStep says which. */
		TestStep,

		Unknown,
	};

	DREAMNARRATIVECOMPILER_API const TCHAR* LexToString(EDnaLineKind Kind);

	enum class EDnaCommandKind : uint8
	{
		None,
		Set,
		Accept,
		Complete,
		Fail,
		Advance,
		Play,
		Emit,
		Wait,
	};

	enum class EDnaOnKind : uint8
	{
		Accept,
		Complete,
		Fail,
		Timeout,
	};

	enum class EDnaTestStepKind : uint8
	{
		None,
		/** `seed n`. Payload.Seed. */
		Seed,
		/** `given`, a block of `fact = value` (Implication) lines and GivenChapter / GivenVisited / GivenOnce lines. */
		Given,
		/** `talk 角色 [strict]`. Payload.Name, bStrict; children are expectations. */
		Talk,
		/** `start 节点 [strict]`. Payload.Name, bStrict; children are expectations. */
		Start,
		/** `expect 条件`. Payload.Condition. */
		Expect,
		/** `set fact = value`. Payload.Name, AssignOp, Expr. */
		Set,
		/** `wait 时长`. Payload.Seconds. */
		Wait,
		/** `jump 章节`. Payload.Target. */
		Jump,
		/** `save`. */
		Save,
		/** A line the dialogue must show: Payload.Name is the speaker (empty for narration) with
		 *  SpeakerTags and Text, or Payload.Target is a line id. */
		ExpectLine,
		/** `absent` + a line the dialogue must not show, in the same fields as ExpectLine; or, with
		 *  bChoice, `absent - 文本` / `absent - #id`: a choice that must not be offered, as Pick. */
		Absent,
		/** `- 文本` or `- #id`: the choice to take. Payload.Text, or Payload.Target for an id. */
		Pick,
		/** `rolls low, high, 0.25`: what the next draws come out as. Payload.Rolls. */
		Rolls,
		/** `depth n` (explore): how many steps from the start at most. Payload.Depth. */
		Depth,
		/** `always 条件` (explore): holds in every state reached. Payload.Condition. */
		Always,
		/** `eventually 条件` (explore): holds in some state reached. Payload.Condition. */
		Eventually,
		/** `softlock 条件或任务, ...` (explore): reachable from every state reached. Payload.Goals. */
		Softlock,
		/** Under `given`: `chapter = 章节`. Payload.Target. */
		GivenChapter,
		/** Under `given`: `visited(对话) = n`. Payload.Target, Count. */
		GivenVisited,
		/** Under `given`: `once #选项id`, a once-choice already taken. Payload.Target. */
		GivenOnce,
	};

	/** The source keyword of a step: `seed`, `talk`, ...; `line`, `absent` and `pick` for the expectations. */
	DREAMNARRATIVECOMPILER_API const TCHAR* LexToString(EDnaTestStepKind Kind);

	/** One run of a text: either a literal or an interpolated `{expression}`. */
	struct FDnaTextPart
	{
		FString Literal;
		FDnaExprPtr Expr;
		bool IsExpr() const { return Expr.IsValid(); }
	};

	/** A localisable text as it appears in source, with its interpolations parsed. */
	struct FDnaText
	{
		FString Raw;
		FDnaSpan Span;
		TArray<FDnaTextPart> Parts;

		bool IsEmpty() const { return Raw.IsEmpty(); }
		bool HasInterpolation() const
		{
			for (const FDnaTextPart& Part : Parts) { if (Part.IsExpr()) { return true; } }
			return false;
		}
	};

	/** The trailing modifiers of a speech, narration or choice line. */
	struct FDnaModifiers
	{
		FDnaExprPtr Condition;
		bool bOnce = false;
		bool bHasDuration = false;
		double DurationSeconds = 0.0;
		FString Stage;
		FString Id;
		/** Offset (into the source) where the modifier tail starts; INDEX_NONE when there is none. */
		int32 TailOffset = INDEX_NONE;
	};

	/** What a line means. Only the members relevant to the line's Kind are set. */
	struct FDnaLinePayload
	{
		/** Declared name / property key / speaker / command target / jump target / track / verb. */
		FString Name;
		FString WasName;
		FString Value;
		FDnaText Text;
		TArray<FString> SpeakerTags;
		FDnaModifiers Modifiers;

		// dialogue header, rules, conditions
		FString ForCharacter;
		FDnaExprPtr Condition;
		int32 Priority = INDEX_NONE;
		FString Target;
		bool bPlayer = false;
		bool bAuto = false;
		bool bOptional = false;
		bool bAsync = false;
		bool bEnd = false;

		// fact
		EDnaFactType FactType = EDnaFactType::Bool;
		bool bInGroup = false;
		bool bExtern = false;
		bool bSession = false;
		EDnaAuthority Authority = EDnaAuthority::Shared;
		bool bHasRange = false;
		double RangeMin = 0.0;
		double RangeMax = 0.0;
		TArray<FString> Domain;
		FDnaExprPtr DefaultExpr;

		// command
		EDnaCommandKind Command = EDnaCommandKind::None;
		EDnaTokenKind AssignOp = EDnaTokenKind::Assign;
		FDnaExprPtr Expr;
		double Seconds = 0.0;
		EDnaOnKind OnKind = EDnaOnKind::Accept;

		// reward
		FString RewardCategory;
		FString RewardItem;
		int32 RewardAmount = 1;

		// stage entry
		double TimeStart = 0.0;
		double TimeEnd = 0.0;
		bool bIsRange = false;
		bool bPercent = false;
		/** Everything after the verb (stage entry) or the name (shot, restore entry), as tokens. */
		TArray<FDnaToken> Args;

		// test
		EDnaTestStepKind TestStep = EDnaTestStepKind::None;
		int32 Seed = 0;
		/** `talk` / `start ... strict`: every shown line must be expected, in order. */
		bool bStrict = false;
		/** An Absent step about a choice (`absent - 文本`) rather than a line. */
		bool bChoice = false;
		/** `rolls`: unit draws in [0, 1); `low` and `high` are LowestDraw and HighestDraw. */
		TArray<float> Rolls;
		/** `depth n`. */
		int32 Depth = 0;
		/** `visited(x) = n` under `given`. */
		int32 Count = 0;
		/** `softlock`: one condition per goal, or a bare quest name standing for its completion. */
		TArray<FDnaExprPtr> Goals;
	};

	struct FDnaLineNode;
	using FDnaLineNodePtr = TSharedPtr<FDnaLineNode>;

	struct DREAMNARRATIVECOMPILER_API FDnaLineNode
	{
		EDnaLineKind Kind = EDnaLineKind::Unknown;
		FDnaRawLine Raw;
		FDnaLinePayload Payload;
		TArray<FDnaLineNodePtr> Children;
		/** Non-owning. */
		FDnaLineNode* Parent = nullptr;

		int32 LineNumber() const { return Raw.LineNumber; }
		const FDnaSpan& Span() const { return Raw.Span; }
		bool IsFloating() const { return Kind == EDnaLineKind::Blank || Kind == EDnaLineKind::CommentOnly; }

		/** True for kinds whose indented lines are children rather than continuations. */
		bool OpensBlock() const;

		/** Children that carry content, in order. */
		void GetContentChildren(TArray<FDnaLineNode*>& Out) const;
	};

	struct DREAMNARRATIVECOMPILER_API FDnaDocument
	{
		FString Path;
		FDnaSourceText Source;
		FDnaScanResult Scan;
		TArray<FDnaLineNodePtr> Roots;
		/** Every name this file declares at top level, including grouped facts as `组.子项`. */
		TSet<FString> DeclaredNames;

		/** Pre-order over every node, floating lines included. */
		void ForEachNode(TFunctionRef<void(FDnaLineNode&)> Visitor);
		void ForEachNode(TFunctionRef<void(const FDnaLineNode&)> Visitor) const;

		/** Top-level declarations of a kind, in order. */
		void GetRootsOfKind(EDnaLineKind Kind, TArray<FDnaLineNode*>& Out) const;
	};
}
