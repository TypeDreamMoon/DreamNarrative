// Copyright DreamMoon. All Rights Reserved.

#include "DnaDreamGameplayTaskMigration.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "DreamNarrativeEditorModule.h"
#include "Engine/Blueprint.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Lang/DnaLexer.h"
#include "Lang/DnaProject.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "DnaDreamGameplayTaskMigration"

namespace UE::DreamNarrative::Editor
{
	namespace
	{
		const FProperty* Find(const UStruct* Type, const TCHAR* Name)
		{
			return Type ? Type->FindPropertyByName(Name) : nullptr;
		}

		FString ReadName(const UStruct* Type, const void* Container, const TCHAR* Name)
		{
			const FNameProperty* Property = CastField<FNameProperty>(Find(Type, Name));
			return Property ? Property->GetPropertyValue_InContainer(Container).ToString() : FString();
		}

		FString ReadText(const UStruct* Type, const void* Container, const TCHAR* Name)
		{
			const FTextProperty* Property = CastField<FTextProperty>(Find(Type, Name));
			FString Text = Property ? Property->GetPropertyValue_InContainer(Container).ToString() : FString();
			Text.ReplaceInline(TEXT("\r"), TEXT(""));
			Text.ReplaceInline(TEXT("\n"), TEXT(" "));
			// Braces would read as interpolation.
			Text.ReplaceInline(TEXT("{"), TEXT("("));
			Text.ReplaceInline(TEXT("}"), TEXT(")"));
			return Text.TrimStartAndEnd();
		}

		bool ReadBool(const UStruct* Type, const void* Container, const TCHAR* Name)
		{
			const FBoolProperty* Property = CastField<FBoolProperty>(Find(Type, Name));
			return Property && Property->GetPropertyValue_InContainer(Container);
		}

		double ReadNumber(const UStruct* Type, const void* Container, const TCHAR* Name)
		{
			const FNumericProperty* Property = CastField<FNumericProperty>(Find(Type, Name));
			if (!Property)
			{
				return 0.0;
			}
			const void* Value = Property->ContainerPtrToValuePtr<void>(Container);
			return Property->IsFloatingPoint() ? Property->GetFloatingPointPropertyValue(Value) : static_cast<double>(Property->GetSignedIntPropertyValue(Value));
		}

		/** An enum's value by its name (`EDTP_High`), whether held as an enum or a byte. */
		FString ReadEnum(const UStruct* Type, const void* Container, const TCHAR* Name)
		{
			const FProperty* Property = Find(Type, Name);
			if (const FEnumProperty* AsEnum = CastField<FEnumProperty>(Property))
			{
				const int64 Value = AsEnum->GetUnderlyingProperty()->GetSignedIntPropertyValue(AsEnum->ContainerPtrToValuePtr<void>(Container));
				return AsEnum->GetEnum()->GetNameStringByValue(Value);
			}
			if (const FByteProperty* AsByte = CastField<FByteProperty>(Property); AsByte && AsByte->Enum)
			{
				return AsByte->Enum->GetNameStringByValue(AsByte->GetPropertyValue_InContainer(Container));
			}
			return FString();
		}

		TArray<const UClass*> ReadClasses(const UStruct* Type, const void* Container, const TCHAR* Name)
		{
			TArray<const UClass*> Out;
			const FArrayProperty* Array = CastField<FArrayProperty>(Find(Type, Name));
			const FClassProperty* Inner = Array ? CastField<FClassProperty>(Array->Inner) : nullptr;
			if (!Inner)
			{
				return Out;
			}
			FScriptArrayHelper Helper(Array, Array->ContainerPtrToValuePtr<void>(Container));
			for (int32 Index = 0; Index < Helper.Num(); ++Index)
			{
				if (const UClass* Class = Cast<UClass>(Inner->GetObjectPropertyValue(Helper.GetRawPtr(Index))))
				{
					Out.Add(Class);
				}
			}
			return Out;
		}

		/** A name the language takes. */
		FString AsName(const FString& Raw)
		{
			FString Out;
			for (const TCHAR Char : Raw)
			{
				Out.AppendChar(Lang::IsDnaIdentifierChar(Char) ? Char : TEXT('_'));
			}
			if (Out.IsEmpty() || !Lang::IsDnaIdentifierStart(Out[0]))
			{
				Out = TEXT("任务_") + Out;
			}
			return Out;
		}

		/** The quest name for a task class: its TaskName, or the class's name without `BP_` and `_C` when it kept the default. */
		FString QuestName(const UClass* Class)
		{
			const UObject* Defaults = Class->GetDefaultObject();
			FString Name = ReadName(Class, Defaults, TEXT("TaskName"));
			if (Name.IsEmpty() || Name == TEXT("Task"))
			{
				Name = Class->GetName();
				Name.RemoveFromEnd(TEXT("_C"));
				Name.RemoveFromStart(TEXT("BP_"));
			}
			return AsName(Name);
		}

		struct FCondition
		{
			FString Key;
			FString Display;
			int32 Target = 1;
		};
	}

	FString MakeDnaFromDreamGameplayTasks(const TArray<const UClass*>& TaskClasses, TArray<FString>& OutNotes)
	{
		TArray<FString> Lines;
		Lines.Add(TEXT("// 从 DreamGameplayTask 迁移。每个任务的条件成了一个计数 fact(游戏照旧往里写数),目标是它到数。"));
		Lines.Add(TEXT("// 蓝图里写的逻辑(接任务、完成时做的事)没有迁过来:照迁移备注改写成任务的 on 块。"));
		for (const UClass* Class : TaskClasses)
		{
			if (!Class)
			{
				continue;
			}
			const UObject* Task = Class->GetDefaultObject();
			const FString Quest = QuestName(Class);
			// The counters' group: a name of its own, as quests and facts share one namespace.
			const FString Counters = Quest + TEXT("_计数");

			// -- the conditions: named counters with a target each
			TArray<FCondition> Conditions;
			FString Mode = TEXT("EDTCCM_All");
			int32 Custom = 1;
			if (const FStructProperty* Container = CastField<FStructProperty>(Find(Class, TEXT("TaskCompletedCondition"))))
			{
				const void* Data = Container->ContainerPtrToValuePtr<void>(Task);
				Mode = ReadEnum(Container->Struct, Data, TEXT("CompletionMode"));
				Custom = FMath::Max(1, static_cast<int32>(ReadNumber(Container->Struct, Data, TEXT("CustomConditionCount"))));
				if (const FMapProperty* Map = CastField<FMapProperty>(Find(Container->Struct, TEXT("Conditions"))))
				{
					FScriptMapHelper Helper(Map, Map->ContainerPtrToValuePtr<void>(Data));
					const FNameProperty* KeyProperty = CastField<FNameProperty>(Map->KeyProp);
					const FObjectPropertyBase* ValueProperty = CastField<FObjectPropertyBase>(Map->ValueProp);
					for (int32 Index = 0; KeyProperty && ValueProperty && Index < Helper.GetMaxIndex(); ++Index)
					{
						if (!Helper.IsValidIndex(Index))
						{
							continue;
						}
						FCondition& Condition = Conditions.AddDefaulted_GetRef();
						Condition.Key = AsName(KeyProperty->GetPropertyValue(Helper.GetKeyPtr(Index)).ToString());
						if (const UObject* Template = ValueProperty->GetObjectPropertyValue(Helper.GetValuePtr(Index)))
						{
							Condition.Display = ReadText(Template->GetClass(), Template, TEXT("ConditionDisplayName"));
							Condition.Target = FMath::Max(1, static_cast<int32>(ReadNumber(Template->GetClass(), Template, TEXT("CompletedCount"))));
						}
					}
				}
			}

			Lines.Add(FString());
			Lines.Add(FString::Printf(TEXT("// %s"), *Class->GetPathName()));
			if (Conditions.Num() > 0)
			{
				Lines.Add(TEXT("fact ") + Counters);
				for (const FCondition& Condition : Conditions)
				{
					Lines.Add(FString::Printf(TEXT("  %s: int extern"), *Condition.Key));
				}
				Lines.Add(FString());
			}
			Lines.Add(TEXT("quest ") + Quest);
			const FString Title = ReadText(Class, Task, TEXT("TaskDisplayName"));
			if (!Title.IsEmpty())
			{
				Lines.Add(TEXT("  title: ") + Title);
			}
			const FString Priority = ReadEnum(Class, Task, TEXT("TaskPriority"));
			Lines.Add(Priority == TEXT("EDTP_High") || Priority == TEXT("EDTP_Urgent") ? TEXT("  priority: main") : TEXT("  priority: side"));
			if (ReadBool(Class, Task, TEXT("bUseMaximumCompletionTime")))
			{
				const double Seconds = ReadNumber(Class, Task, TEXT("MaximumCompletionTimeSeconds"));
				if (Seconds > 0.0)
				{
					Lines.Add(FString::Printf(TEXT("  timeout: %ds"), FMath::Max(1, FMath::RoundToInt(Seconds))));
				}
			}
			const FString Description = ReadText(Class, Task, TEXT("TaskDesc"));
			if (!Description.IsEmpty())
			{
				Lines.Add(TEXT("  journal"));
				Lines.Add(TEXT("    accepted: ") + Description);
			}
			if (Mode == TEXT("EDTCCM_Any") && Conditions.Num() > 1)
			{
				// Any one of them: one objective that holds when any counter has reached its target.
				TArray<FString> Any;
				for (const FCondition& Condition : Conditions)
				{
					Any.Add(FString::Printf(TEXT("%s.%s >= %d"), *Counters, *Condition.Key, Condition.Target));
				}
				Lines.Add(TEXT("  objective ") + FString::Join(Any, TEXT(" or ")));
			}
			else
			{
				if (Mode == TEXT("EDTCCM_Custom") && Custom < Conditions.Num())
				{
					OutNotes.Add(FString::Printf(TEXT("%s: completes on %d of its %d conditions in DGT; migrated as all of them -- mark the ones it can do without `optional`"),
						*Quest, Custom, Conditions.Num()));
				}
				for (const FCondition& Condition : Conditions)
				{
					Lines.Add(FString::Printf(TEXT("  objective %s.%s >= %d"), *Counters, *Condition.Key, Condition.Target));
					if (!Condition.Display.IsEmpty())
					{
						Lines.Add(FString::Printf(TEXT("    show: %s {%s.%s}/%d"), *Condition.Display, *Counters, *Condition.Key, Condition.Target));
					}
				}
			}
			const bool bAuto = ReadBool(Class, Task, TEXT("bCompletedAutoGiveSubTask"));
			for (const UClass* Sub : ReadClasses(Class, Task, TEXT("SubTasks")))
			{
				Lines.Add(FString::Printf(TEXT("  subquest %s%s"), *QuestName(Sub), bAuto ? TEXT(" auto") : TEXT("")));
				if (!TaskClasses.Contains(Sub))
				{
					OutNotes.Add(FString::Printf(TEXT("%s: its sub-task %s was not among the tasks migrated"), *Quest, *Sub->GetName()));
				}
			}
			if (const UBlueprint* Blueprint = Cast<UBlueprint>(Class->ClassGeneratedBy))
			{
				if (Blueprint->UbergraphPages.Num() > 0 || Blueprint->FunctionGraphs.Num() > 0)
				{
					OutNotes.Add(FString::Printf(TEXT("%s: its Blueprint has graphs (%s); what they do on accept, completion or failure goes into `on accept` / `on complete` / `on fail`"),
						*Quest, *Blueprint->GetName()));
				}
			}
		}
		return FString::Join(Lines, TEXT("\n")) + TEXT("\n");
	}

	TArray<const UClass*> FindDreamGameplayTaskClasses()
	{
		TArray<const UClass*> Out;
		const UClass* Base = FindFirstObject<UClass>(TEXT("DreamTask"), EFindFirstObjectOptions::ExactClass);
		if (!Base)
		{
			return Out;
		}
		// Load every Blueprint deriving from it, then take the classes.
		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		TSet<FTopLevelAssetPath> Derived;
		Registry.GetDerivedClassNames({ Base->GetClassPathName() }, {}, Derived);
		TArray<FAssetData> Blueprints;
		Registry.GetAssetsByClass(UBlueprint::StaticClass()->GetClassPathName(), Blueprints, /*bSearchSubClasses=*/true);
		for (const FAssetData& Asset : Blueprints)
		{
			const FString Generated = Asset.GetTagValueRef<FString>(FBlueprintTags::GeneratedClassPath);
			if (!Generated.IsEmpty() && Derived.Contains(FTopLevelAssetPath(FSoftObjectPath(FPackageName::ExportTextPathToObjectPath(Generated)).GetAssetPath())))
			{
				if (const UBlueprint* Blueprint = Cast<UBlueprint>(Asset.GetAsset()); Blueprint && Blueprint->GeneratedClass && !Blueprint->GeneratedClass->HasAnyClassFlags(CLASS_Abstract))
				{
					Out.AddUnique(Blueprint->GeneratedClass.Get());
				}
			}
		}
		return Out;
	}

	FString MigrateDreamGameplayTasks(TArray<FString>& OutNotes, FText& OutError)
	{
		const TArray<const UClass*> Classes = FindDreamGameplayTaskClasses();
		if (Classes.Num() == 0)
		{
			OutError = LOCTEXT("NoTasks", "The project has no DreamGameplayTask tasks to migrate");
			return FString();
		}
		const FString Text = MakeDnaFromDreamGameplayTasks(Classes, OutNotes);
		const FString File = FPaths::Combine(Lang::GetDnaSourceRoot(), TEXT("Migrated"), TEXT("DreamGameplayTask.dna"));
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), /*Tree=*/true);
		if (!FFileHelper::SaveStringToFile(Text, *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			OutError = FText::Format(LOCTEXT("CannotWrite", "Cannot write {0}"), FText::FromString(File));
			return FString();
		}
		return File;
	}

	namespace
	{
		FAutoConsoleCommand MigrateCommand(
			TEXT("dn.migratedgt"),
			TEXT("Writes the project's DreamGameplayTask tasks as .dna quests under <source folder>/Migrated"),
			FConsoleCommandDelegate::CreateLambda([]()
			{
				TArray<FString> Notes;
				FText Error;
				const FString File = MigrateDreamGameplayTasks(Notes, Error);
				if (File.IsEmpty())
				{
					UE_LOG(LogDreamNarrativeEditor, Warning, TEXT("%s"), *Error.ToString());
					return;
				}
				UE_LOG(LogDreamNarrativeEditor, Display, TEXT("Migrated into %s"), *File);
				for (const FString& Note : Notes)
				{
					UE_LOG(LogDreamNarrativeEditor, Display, TEXT("  %s"), *Note);
				}
			}));
	}
}

#undef LOCTEXT_NAMESPACE
