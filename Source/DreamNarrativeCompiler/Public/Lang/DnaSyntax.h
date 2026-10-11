// Copyright DreamMoon. All Rights Reserved.
//
// The concrete syntax tree. Because the grammar is line-oriented, the tree IS the lines: every node
// is one physical line (its indentation, its content, its comment, all kept verbatim) plus the
// parsed meaning of that line, and children are the lines indented under it. Blank and comment-only
// lines are nodes too. The printer re-emits an unedited node from its raw line and only renders the
// nodes an editor changed, which is what keeps write-back format-preserving.

#pragma once

#include "CoreMinimal.h"
#include "DreamNarrativeEvent.h"
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
		/** `channel 名` opening a block of properties. */
		Channel,
		/** `event 名` opening a block of parameters (or standing alone: an event that carries nothing). */
		Event,
		/** `名: type ...` inside an event: one parameter, read like a grouped fact. */
		EventParam,
		/** `archive 名 [profile]` opening a block of a title and entries. */
		Archive,
		/** `entry [if 条件]` inside an archive; the lines under it are its text. */
		ArchiveEntry,

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
		/** Under `given`: `quest 任务 = 状态 [剩余时长]`. Payload.Name, Value (the state), Seconds (0: none given). */
		GivenQuest,
		/** `accept 任务`, `complete 任务`, `fail 任务`: the game asks the quest. Payload.Name. */
		QuestAccept,
		QuestComplete,
		QuestFail,
		/** `journal 任务: 文本`: the quest's journal line reads so now. Payload.Name, Text. */
		Journal,
		/** `emit 事件(参数 = 值, ...) [hold]`: the game raises an event. Payload.Name, EmitArgs, bHold; children are expectations. */
		Emit,
		/** `channel 频道 [strict]` under a talk, start or emit: the lines that channel shows. Payload.Name, bStrict; children are expectations. */
		ChannelLines,
		/** `newgame`: the game starts a new playthrough. */
		NewGame,
		/** `select 章节`: the game goes back to the chapter's checkpoint. Payload.Target. */
		Select,
		/** `archive 资料: 文本` (that entry is unlocked; Count is INDEX_NONE) or `archive 资料 = n` (n entries are). Payload.Name, Text, Count. */
		Archive,
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

	/** `参数 = 值` in an `emit`. */
	struct FDnaEmitArg
	{
		FString Name;
		FDnaExprPtr Value;
		FDnaSpan Span;
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
		/** `voice 资源`: the line's own voice, a content path or a name; overrides the speaker's lookup. */
		FString Voice;
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
		/** `on 事件` (dialogue header). */
		FString OnEvent;
		/** `channel 频道` (dialogue header). */
		FString Channel;
		/** `once` (dialogue header). */
		bool bOnceEntry = false;
		/** `cooldown 时长` (dialogue header); 0 for none. */
		double CooldownSeconds = 0.0;
		/** `resume` (dialogue header). */
		bool bResume = false;
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
		/** `profile` (a fact, a fact group or an archive) and `progress` (a fact or a fact group). */
		bool bProfile = false;
		bool bProgress = false;
		/** `gameplay 标签` on a fact or an event header. */
		FString GameplayTag;
		/** `from ...` on an event parameter, and its tag for the tag sources. */
		EDnaGameplaySource GameplaySource = EDnaGameplaySource::None;
		FString GameplaySourceTag;

		// command
		EDnaCommandKind Command = EDnaCommandKind::None;
		EDnaTokenKind AssignOp = EDnaTokenKind::Assign;
		FDnaExprPtr Expr;
		double Seconds = 0.0;
		EDnaOnKind OnKind = EDnaOnKind::Accept;
		/** `emit 事件(参数 = 值, ...)`, as a command or a test step. */
		TArray<FDnaEmitArg> EmitArgs;
		/** A test's `emit ... hold`: what it opens stays on its first line. */
		bool bHold = false;

		// reward
		FString RewardCategory;
		FString RewardItem;
		int32 RewardAmount = 1;

		// stage entry
		double TimeStart = 0.0;
		double TimeEnd = 0.0;
		bool bIsRange = false;
		/** The start (bPercent) and the end (bEndPercent) are percentages; bBareTime: one of them is a number with no unit. */
		bool bPercent = false;
		bool bEndPercent = false;
		bool bBareTime = false;
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
