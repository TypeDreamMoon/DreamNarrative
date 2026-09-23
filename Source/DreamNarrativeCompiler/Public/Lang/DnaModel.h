// Copyright DreamMoon. All Rights Reserved.
//
// The semantic model: every declaration across every document of a project, by name. The parser
// sees one file; this is where "the project" exists. Building it reports the cross-file errors
// (duplicate names, `was` pointing at a name that still exists, reserved words used as names) and
// the validator then checks every reference and every type against it.

#pragma once

#include "CoreMinimal.h"
#include "DreamNarrativeTypes.h"
#include "Lang/DnaDiagnostic.h"
#include "Lang/DnaSyntax.h"

namespace UE::DreamNarrative::Lang
{
	struct FDnaDeclBase
	{
		FString Name;
		FString WasName;
		/** Non-owning; the document outlives the program. */
		FDnaLineNode* Node = nullptr;
		FDnaDocument* Document = nullptr;
	};

	struct FDnaFactInfo : FDnaDeclBase
	{
		EDnaFactType Type = EDnaFactType::Bool;
		bool bExtern = false;
		bool bSession = false;
		EDnaAuthority Authority = EDnaAuthority::Shared;
		bool bHasRange = false;
		double RangeMin = 0.0;
		double RangeMax = 0.0;
		TArray<FString> Domain;
		FDnaExprPtr DefaultExpr;

		bool HasMember(const FString& Member) const { return Domain.Contains(Member); }
	};

	struct FDnaCharacterInfo : FDnaDeclBase
	{
		bool bPlayer = false;
		FString Display;
		FString VoiceRoot;
		TMap<FString, FString> Meta;
	};

	struct FDnaChapterInfo : FDnaDeclBase
	{
		FString After;
		FString Title;
	};

	struct FDnaQuestInfo : FDnaDeclBase
	{
	};

	struct FDnaDialogueInfo : FDnaDeclBase
	{
		FString ForCharacter;
		FDnaExprPtr Condition;
		int32 Priority = INDEX_NONE;
	};

	struct FDnaStageInfo : FDnaDeclBase
	{
		/** `base: line` -- times are percentages of the line the stage is attached to. */
		bool bLineRelative = false;
		double DurationSeconds = 0.0;
	};

	/**
	 * A `test` or `explore` block. The two share a namespace of their own: a test may share its name
	 * with a dialogue, not with an explore block.
	 */
	struct FDnaTestInfo : FDnaDeclBase
	{
	};

	class DREAMNARRATIVECOMPILER_API FDnaProgram
	{
	public:
		TArray<TSharedPtr<FDnaDocument>> Documents;

		/** Facts by their full dotted name (`墨梅册.第一页`). */
		TMap<FString, FDnaFactInfo> Facts;
		TMap<FString, FDnaCharacterInfo> Characters;
		/** In declaration order across documents, which is what `after` defaults to. */
		TArray<FString> ChapterOrder;
		TMap<FString, FDnaChapterInfo> Chapters;
		TMap<FString, FDnaQuestInfo> Quests;
		TMap<FString, FDnaDialogueInfo> Dialogues;
		TMap<FString, FDnaStageInfo> Stages;
		TMap<FString, FDnaTestInfo> Tests;
		TMap<FString, FDnaTestInfo> Explores;

		const FDnaFactInfo* FindFact(const FString& DottedName) const { return Facts.Find(DottedName); }
		bool IsQuest(const FString& Name) const { return Quests.Contains(Name); }
		bool IsChapter(const FString& Name) const { return Chapters.Contains(Name); }
		bool IsDialogue(const FString& Name) const { return Dialogues.Contains(Name); }
		bool IsStage(const FString& Name) const { return Stages.Contains(Name); }
		bool IsCharacter(const FString& Name) const { return Characters.Contains(Name); }

		/** True when any declaration of any kind uses this name. Out parameter says which kind. */
		bool IsDeclared(const FString& Name, FString* OutKind = nullptr) const;
	};

	/**
	 * Collects the declarations of every document into the program.
	 *
	 *   DNS3004  the same name declared twice (any kind, any file)
	 *   DNS3007  `was 旧名` but the old name is still declared
	 *   DNS3008  a reserved word used as a name
	 */
	DREAMNARRATIVECOMPILER_API bool BuildDnaProgram(const TArray<TSharedPtr<FDnaDocument>>& Documents, FDnaProgram& OutProgram, FDnaDiagnosticSink& Diagnostics);
}
