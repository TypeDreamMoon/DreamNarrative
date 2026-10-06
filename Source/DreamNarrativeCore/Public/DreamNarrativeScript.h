// Copyright DreamMoon. All Rights Reserved.
//
// The compiled form of one `.dna` file. One source file is one of these assets; the compiler is the
// only thing that writes it, and it embeds the source text so the asset can always say where it
// came from and `dnc extract` can hand it back.
//
// Core knows about facts, characters and chapters. Everything a product line adds (dialogue nodes,
// quests, stages) lives in a section subclass declared by that module, so a project that enables
// only Story never loads Stage data and vice versa.

#pragma once

#include "CoreMinimal.h"
#include "DreamNarrativeEvent.h"
#include "DreamNarrativeExpression.h"
#include "DreamNarrativeTypes.h"
#include "Engine/DataAsset.h"
#include "Templates/SubclassOf.h"
#include "DreamNarrativeScript.generated.h"

class UDreamNarrativeScript;

/** A constant a chapter's `implied` block assigns. */
USTRUCT(BlueprintType)
struct DREAMNARRATIVECORE_API FDnaImpliedFact
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Narrative")
	FName Fact;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Narrative")
	FDnaValue Value;
};

/** `advance -> Target if Condition`. */
USTRUCT(BlueprintType)
struct DREAMNARRATIVECORE_API FDnaAdvanceRule
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Narrative")
	FName Target;

	/** Index into the owning script's expression pool; INDEX_NONE means unconditional. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Narrative")
	int32 ConditionExpr = INDEX_NONE;
};

USTRUCT(BlueprintType)
struct DREAMNARRATIVECORE_API FDnaChapterDecl
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Narrative")
	FName Name;

	/** The chapter whose implied facts precede this one's when jumping. None for a first chapter. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Narrative")
	FName After;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Narrative")
	FText Title;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Narrative")
	TArray<FDnaImpliedFact> Implied;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Narrative")
	TArray<FDnaAdvanceRule> Advance;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Narrative")
	FName WasName;
};

/** Base of the per-product-line payloads a script carries. */
UCLASS(Abstract, BlueprintType, DefaultToInstanced, EditInlineNew)
class DREAMNARRATIVECORE_API UDreamNarrativeScriptSection : public UObject
{
	GENERATED_BODY()

public:
	/** The script this section belongs to. */
	UFUNCTION(BlueprintPure, Category = "Narrative")
	UDreamNarrativeScript* GetScript() const;
};

UCLASS(BlueprintType)
class DREAMNARRATIVECORE_API UDreamNarrativeScript : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Project-relative path of the source file this was compiled from. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Source")
	FString SourcePath;

	/** The source text, verbatim. `dnc extract` writes it back out. */
	UPROPERTY(VisibleAnywhere, Category = "Source")
	FString SourceText;

	/** Hash of SourceText, so a build can tell an up-to-date asset from a stale one. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Source")
	FString SourceHash;

	/** Version of the compiled layout; bumped when a change makes older assets unreadable. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Source")
	int32 SchemaVersion = 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Declarations")
	TArray<FDnaFactDecl> Facts;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Declarations")
	TArray<FDnaCharacterDecl> Characters;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Declarations")
	TArray<FDnaChapterDecl> Chapters;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Declarations")
	TArray<FDnaEventDecl> Events;

	/** Every expression in the file, referenced by index from sections and chapter rules. */
	UPROPERTY(VisibleAnywhere, Category = "Compiled")
	TArray<FDnaCompiledExpr> Expressions;

	UPROPERTY(VisibleAnywhere, Instanced, BlueprintReadOnly, Category = "Compiled")
	TArray<TObjectPtr<UDreamNarrativeScriptSection>> Sections;

	/** The first section of a class, or null. */
	UDreamNarrativeScriptSection* FindSection(TSubclassOf<UDreamNarrativeScriptSection> Class) const;

	template <typename T>
	T* FindSection() const { return Cast<T>(FindSection(T::StaticClass())); }

	const FDnaCompiledExpr* GetExpression(int32 Index) const { return Expressions.IsValidIndex(Index) ? &Expressions[Index] : nullptr; }

	/** MD5 of a source text, as SourceHash stores it. */
	static FString HashSource(const FString& Source);
};
