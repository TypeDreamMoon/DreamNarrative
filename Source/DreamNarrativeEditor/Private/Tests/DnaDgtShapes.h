// Copyright DreamMoon. All Rights Reserved.
//
// Stand-ins shaped like DreamGameplayTask's task, condition and condition container -- the same
// property names and enum value names, nothing else -- so the migration's reflection can be tested
// in a project without DGT. Hidden from every class picker.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "DnaDgtShapes.generated.h"

UENUM()
enum class EDnaDgtShapePriority : uint8
{
	EDTP_Low,
	EDTP_Normal,
	EDTP_High,
	EDTP_Urgent,
};

UENUM()
enum class EDnaDgtShapeCompletion : uint8
{
	EDTCCM_All,
	EDTCCM_Any,
	EDTCCM_Custom,
};

UCLASS(Hidden, NotBlueprintable, Transient)
class UDnaDgtShapeCondition : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FText ConditionDisplayName;

	UPROPERTY()
	int32 Count = 0;

	UPROPERTY()
	int32 CompletedCount = 1;

	UPROPERTY()
	bool bTaskMustBeCompleted = false;
};

USTRUCT()
struct FDnaDgtShapeConditions
{
	GENERATED_BODY()

	UPROPERTY()
	TMap<FName, TObjectPtr<UDnaDgtShapeCondition>> Conditions;

	UPROPERTY()
	EDnaDgtShapeCompletion CompletionMode = EDnaDgtShapeCompletion::EDTCCM_All;

	UPROPERTY()
	int32 CustomConditionCount = 1;
};

UCLASS(Hidden, NotBlueprintable, Transient)
class UDnaDgtShapeTask : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FName TaskName = TEXT("Task");

	UPROPERTY()
	FText TaskDisplayName;

	UPROPERTY()
	FText TaskDesc;

	UPROPERTY()
	TArray<TSubclassOf<UObject>> SubTasks;

	UPROPERTY()
	bool bCompletedAutoGiveSubTask = false;

	UPROPERTY()
	EDnaDgtShapePriority TaskPriority = EDnaDgtShapePriority::EDTP_Normal;

	UPROPERTY()
	bool bUseMaximumCompletionTime = false;

	UPROPERTY()
	float MaximumCompletionTimeSeconds = 0.f;

	UPROPERTY()
	FDnaDgtShapeConditions TaskCompletedCondition;
};

UCLASS(Hidden, NotBlueprintable, Transient)
class UDnaDgtShapeSubTask : public UDnaDgtShapeTask
{
	GENERATED_BODY()
};
