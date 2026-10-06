// Copyright DreamMoon. All Rights Reserved.
//
// The value model the whole plugin agrees on. A fact is a typed slot in the ledger; a value is what
// sits in it. Both the compiler (which declares facts from source) and the runtime (which stores and
// evaluates them) speak in these terms, so they live in Core and nowhere else.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "DreamNarrativeTypes.generated.h"

/** The four value types a fact can hold. Strings are deliberately absent: a narrative condition that
 *  compares strings is a smell, and a tag with a declared domain covers every legitimate case. */
UENUM(BlueprintType)
enum class EDnaFactType : uint8
{
	Bool,
	Int,
	Float,
	/** One value out of the set the fact declared with `in (...)`. Held as a bare name. */
	Tag,
};

/** Who is allowed to write a fact. Narrative facts are written only by compiled source; game code
 *  writing one is a runtime error. Extern facts are fed by game systems and may also be written by
 *  source (resetting a counter on quest accept, say). */
UENUM(BlueprintType)
enum class EDnaFactOwner : uint8
{
	Narrative,
	Extern,
};

/** Who is writing: compiled narrative, or game code. Decides whether the ownership rule applies. */
UENUM(BlueprintType)
enum class EDnaWriteSource : uint8
{
	Narrative,
	Game,
};

/** Network authority, recorded from the declaration and not yet enforced (Architecture §9). */
UENUM(BlueprintType)
enum class EDnaAuthority : uint8
{
	Shared,
	Server,
	Client,
};

/** A value in the ledger. Only the member matching Type is meaningful. SaveGame, because snapshots hold values. */
USTRUCT(BlueprintType)
struct DREAMNARRATIVECORE_API FDnaValue
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "Narrative")
	EDnaFactType Type = EDnaFactType::Bool;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "Narrative")
	bool bBool = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "Narrative")
	int32 Int = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "Narrative")
	float Float = 0.f;

	/** For Tag values: the bare member name as declared in the fact's domain, e.g. `盛开`. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "Narrative")
	FName Tag;

	static FDnaValue FromBool(bool bValue) { FDnaValue V; V.Type = EDnaFactType::Bool; V.bBool = bValue; return V; }
	static FDnaValue FromInt(int32 Value) { FDnaValue V; V.Type = EDnaFactType::Int; V.Int = Value; return V; }
	static FDnaValue FromFloat(float Value) { FDnaValue V; V.Type = EDnaFactType::Float; V.Float = Value; return V; }
	static FDnaValue FromTag(FName Value) { FDnaValue V; V.Type = EDnaFactType::Tag; V.Tag = Value; return V; }

	/** Default value of a type: false, 0, 0.0, or none. */
	static FDnaValue DefaultFor(EDnaFactType InType) { FDnaValue V; V.Type = InType; return V; }

	bool IsNumeric() const { return Type == EDnaFactType::Int || Type == EDnaFactType::Float; }

	/** Truthiness: false/0/0.0/NAME_None are false. */
	bool AsBool() const;

	/** Numeric view; Bool yields 0/1, Tag yields 0. */
	double AsNumber() const;

	/** Value equality with numeric promotion (1 == 1.0). Different non-numeric types are never equal. */
	bool Equals(const FDnaValue& Other) const;

	/** Source-form spelling: `true`, `5`, `0.5`, `盛开`. */
	FString ToString() const;
};

/** A fact as declared in source: its slot type and the rules around writing it. */
USTRUCT(BlueprintType)
struct DREAMNARRATIVECORE_API FDnaFactDecl
{
	GENERATED_BODY()

	/** The declared name, dotted for grouped facts: `墨梅册.第一页`. Unique across the project. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Narrative")
	FName Name;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Narrative")
	EDnaFactType Type = EDnaFactType::Bool;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Narrative")
	EDnaFactOwner Owner = EDnaFactOwner::Narrative;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Narrative")
	EDnaAuthority Authority = EDnaAuthority::Shared;

	/** Session facts are not part of the snapshot. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Narrative")
	bool bSession = false;

	/** `profile`: kept across save slots and playthroughs, in a snapshot of its own (MakeProfileSnapshot). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Narrative")
	bool bProfile = false;

	/** `progress`: a save-slot fact that selecting a chapter keeps as it is; checkpoints leave it out. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Narrative")
	bool bProgress = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Narrative")
	FDnaValue Default;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Narrative")
	bool bHasRange = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Narrative")
	double RangeMin = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Narrative")
	double RangeMax = 0.0;

	/** For Tag facts: the members declared with `in (...)`, in declaration order. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Narrative")
	TArray<FName> Domain;

	/** The previous name when the declaration carries `was`, so a snapshot written under it migrates. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Narrative")
	FName WasName;

	/**
	 * `gameplay 标签` on an extern fact: the gameplay tag whose count on a watched ability system
	 * feeds it (a bool holds while the count is above zero, an int is the count). None for none.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Narrative")
	FName GameplayTag;
};

/** A speaker as declared with `character`. */
USTRUCT(BlueprintType)
struct DREAMNARRATIVECORE_API FDnaCharacterDecl
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Narrative")
	FName Name;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Narrative")
	FText Display;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Narrative")
	bool bPlayer = false;

	/** Root under which voice assets are looked up by line id. Empty when the character has none. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Narrative")
	FString VoiceRoot;

	/** Every other `key: value` the declaration carried, passed through to the project untouched. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Narrative")
	TMap<FName, FString> Meta;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Narrative")
	FName WasName;
};

/**
 * Where a quest stands. The state lives in the ledger, as the tag fact QuestStateFact(quest), so a
 * quest saves, loads and drives chapter rules the way any fact does.
 */
UENUM(BlueprintType)
enum class EDnaQuestState : uint8
{
	Inactive,
	/** Taken on; nothing it watches has moved yet. */
	Accepted,
	/** Taken on, and something it watches has moved since. */
	Going,
	Completed,
	Failed,
	/** Ran out of time. Counts as failed. */
	TimedOut,
};

namespace DreamNarrative
{
	/**
	 * The ledger facts a quest keeps its state in, declared by the compiler with every quest. `@` is
	 * not a character a source name can hold, so they never collide with a fact the story declares.
	 *   @猎狼             tag in (Inactive, Accepted, Going, Completed, Failed, TimedOut)
	 *   @猎狼.objectives  int: which objectives are done, bit n for the (n+1)th
	 * The quest's timeout runs on the ledger timer of the same name as its state fact.
	 */
	DREAMNARRATIVECORE_API FName QuestStateFact(FName Quest);
	DREAMNARRATIVECORE_API FName QuestObjectivesFact(FName Quest);
	/** `@archive:名`: which entries of an archive are unlocked, bit n for the (n+1)th; a profile fact when the archive is. */
	DREAMNARRATIVECORE_API FName ArchiveFact(FName Archive);
	/** The member of the state fact's set that stands for State: its enumerator's name. */
	DREAMNARRATIVECORE_API FName QuestStateMember(EDnaQuestState State);
	DREAMNARRATIVECORE_API bool TryParseQuestState(FName Member, EDnaQuestState& OutState);

	namespace Tags
	{
		/** `Narrative.Fact.<name>` -- the gameplay tag a fact is exported under. */
		DREAMNARRATIVECORE_API FString MakeFactTagString(FName FactName);
		/** `Narrative.Enum.<fact>.<member>` -- the gameplay tag a tag-fact member is exported under. */
		DREAMNARRATIVECORE_API FString MakeEnumTagString(FName FactName, FName Member);
		/** `Narrative.Chapter.<name>`. */
		DREAMNARRATIVECORE_API FString MakeChapterTagString(FName ChapterName);
		/** `Narrative.Event.<name>` -- what `emit` broadcasts. */
		DREAMNARRATIVECORE_API FString MakeEventTagString(FName EventName);
	}

	/** Source spelling of a fact type: `bool`, `int`, `float`, `tag`. */
	DREAMNARRATIVECORE_API const TCHAR* LexToString(EDnaFactType Type);
	/** True and the type when Text spells one of the four fact types. */
	DREAMNARRATIVECORE_API bool TryParseFactType(const FString& Text, EDnaFactType& OutType);
}
