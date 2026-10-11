// Copyright DreamMoon. All Rights Reserved.

#include "Lang/DnaLocalization.h"
#include "DreamNarrativeDialogue.h"
#include "DreamNarrativeScript.h"
#include "Internationalization/Text.h"
#include "Internationalization/TextLocalizationResource.h"
#include "Lang/DnaProject.h"
#include "Lang/DnaVoice.h"
#include "Misc/Paths.h"
#include "UObject/UObjectHash.h"
#include "UObject/UnrealType.h"
#if WITH_EDITOR
#include "Internationalization/InternationalizationArchive.h"
#include "Internationalization/InternationalizationManifest.h"
#include "LocTextHelper.h"
#endif

#define LOCTEXT_NAMESPACE "DnaLocalization"

namespace UE::DreamNarrative::Lang
{
	namespace
	{
		using FTextVisitor = TFunctionRef<void(const FText&)>;

		void VisitStructTexts(const UStruct* Struct, const void* Container, FTextVisitor Visit);

		void VisitValueTexts(const FProperty* Property, const void* Value, FTextVisitor Visit)
		{
			if (const FTextProperty* AsText = CastField<FTextProperty>(Property))
			{
				Visit(AsText->GetPropertyValue(Value));
			}
			else if (const FStructProperty* AsStruct = CastField<FStructProperty>(Property))
			{
				VisitStructTexts(AsStruct->Struct, Value, Visit);
			}
			else if (const FArrayProperty* AsArray = CastField<FArrayProperty>(Property))
			{
				FScriptArrayHelper Helper(AsArray, Value);
				for (int32 Index = 0; Index < Helper.Num(); ++Index)
				{
					VisitValueTexts(AsArray->Inner, Helper.GetRawPtr(Index), Visit);
				}
			}
			else if (const FMapProperty* AsMap = CastField<FMapProperty>(Property))
			{
				FScriptMapHelper Helper(AsMap, Value);
				for (int32 Index = 0; Index < Helper.GetMaxIndex(); ++Index)
				{
					if (Helper.IsValidIndex(Index)) { VisitValueTexts(AsMap->ValueProp, Helper.GetValuePtr(Index), Visit); }
				}
			}
		}

		void VisitStructTexts(const UStruct* Struct, const void* Container, FTextVisitor Visit)
		{
			for (TFieldIterator<FProperty> It(Struct); It; ++It)
			{
				for (int32 Index = 0; Index < It->ArrayDim; ++Index)
				{
					VisitValueTexts(*It, It->ContainerPtrToValuePtr<void>(Container, Index), Visit);
				}
			}
		}

		/** Splits CSV text into records: a quoted field may hold line breaks. */
		void SplitRecords(const FString& Text, TArray<FString>& OutRecords)
		{
			TArray<FString> Lines;
			Text.ParseIntoArrayLines(Lines, false);
			FString Pending;
			int32 Quotes = 0;
			for (const FString& Line : Lines)
			{
				Pending = Pending.IsEmpty() && Quotes == 0 ? Line : Pending + TEXT("\n") + Line;
				for (const TCHAR Char : Line) { Quotes += Char == TEXT('"') ? 1 : 0; }
				if (Quotes % 2 == 0)
				{
					if (!Pending.TrimStartAndEnd().IsEmpty()) { OutRecords.Add(Pending); }
					Pending.Reset();
					Quotes = 0;
				}
			}
			if (!Pending.IsEmpty()) { OutRecords.Add(Pending); }
		}

#if WITH_EDITOR
		/** The target's manifest and the archives Culture needs, loaded; null, and why, when the target has not been gathered. */
		TSharedPtr<FLocTextHelper> OpenTarget(const FDnaLocTarget& Target, const FString& Culture, FText& OutError)
		{
			FString Native = Target.NativeCulture;
			if (Native.IsEmpty())
			{
				FTextLocalizationMetaDataResource Meta;
				if (Meta.LoadFromFile(Target.Directory / Target.Name + TEXT(".locmeta"))) { Native = Meta.NativeCulture; }
			}
			TSharedPtr<FLocTextHelper> Helper = MakeShared<FLocTextHelper>(Target.Directory, Target.Name + TEXT(".manifest"), Target.Name + TEXT(".archive"),
				Native, TArray<FString>{ Culture }, nullptr);
			FText Why;
			if (!Helper->LoadManifest(ELocTextHelperLoadFlags::Load, &Why))
			{
				OutError = FText::Format(LOCTEXT("NoManifest", "The localization target {0} has not been gathered ({1}); gather it in the Localization Dashboard first"),
					FText::FromString(Target.Directory), Why);
				return nullptr;
			}
			if (!Native.IsEmpty() && Native != Culture && !Helper->LoadArchive(Native, ELocTextHelperLoadFlags::LoadOrCreate, &Why))
			{
				OutError = Why;
				return nullptr;
			}
			if (!Helper->LoadArchive(Culture, ELocTextHelperLoadFlags::LoadOrCreate, &Why))
			{
				OutError = Why;
				return nullptr;
			}
			return Helper;
		}
#endif
	}

	void CollectDnaLocTexts(const TArray<UDreamNarrativeScript*>& Scripts, TArray<FDnaLocRow>& OutRows)
	{
		OutRows.Reset();
		TSet<FString> Seen;
		const FString SourceRoot = GetDnaSourceRoot() + TEXT("/");
		for (const UDreamNarrativeScript* Script : Scripts)
		{
			if (!Script)
			{
				continue;
			}
			FString File = Script->SourcePath;
			FPaths::NormalizeFilename(File);
			if (FPaths::IsUnderDirectory(File, SourceRoot)) { FPaths::MakePathRelativeTo(File, *SourceRoot); }

			// Lines and choices, by key: who says them, where, after what.
			TMap<FString, FDnaLocRow> Contexts;
			if (const UDnaDialogueSection* Section = Script->FindSection<UDnaDialogueSection>())
			{
				for (const FDnaDialogueNodeData& Node : Section->Nodes)
				{
					FString Previous;
					for (const FDnaLineData& Line : Node.Lines)
					{
						FDnaLocRow& Context = Contexts.Add(Line.Id.ToString());
						Context.Kind = TEXT("line");
						Context.Character = Line.Speaker.IsNone() ? FString() : Line.Speaker.ToString();
						Context.Dialogue = Node.Name.ToString();
						Context.Previous = Previous;
						Context.Tags = FString::JoinBy(Line.Tags, TEXT(" "), [](const FName& Tag) { return Tag.ToString(); });
						Context.Line = Line.SourceLine;
						Previous = Line.Text.ToString();
					}
					for (const FDnaChoiceGroupData& Group : Node.ChoiceGroups)
					{
						for (const FDnaChoiceData& Option : Group.Options)
						{
							FDnaLocRow& Context = Contexts.Add(Option.Id.ToString());
							Context.Kind = TEXT("choice");
							Context.Dialogue = Node.Name.ToString();
							Context.Line = Option.SourceLine;
						}
					}
				}
			}

			// The texts themselves, as the engine's gatherer finds them: every FText the script and its sections hold.
			TArray<const UObject*> Objects = { Script };
			TArray<UObject*> Inner;
			GetObjectsWithOuter(Script, Inner, /*bIncludeNestedObjects=*/true);
			Objects.Append(Inner);
			for (const UObject* Object : Objects)
			{
				VisitStructTexts(Object->GetClass(), Object, [&](const FText& Text)
				{
					const TOptional<FString> Namespace = FTextInspector::GetNamespace(Text);
					const TOptional<FString> Key = FTextInspector::GetKey(Text);
					const FString* Source = FTextInspector::GetSourceString(Text);
					if (!Text.ShouldGatherForLocalization() || !Namespace.IsSet() || !Key.IsSet() || Key->IsEmpty() || !Source)
					{
						return;
					}
					bool bSeen = false;
					Seen.Add(*Namespace + TEXT("\x1F") + *Key, &bSeen);
					if (bSeen)
					{
						return;
					}
					FDnaLocRow& Row = OutRows.AddDefaulted_GetRef();
					if (const FDnaLocRow* Context = Contexts.Find(*Key)) { Row = *Context; }
					else { Row.Kind = TEXT("text"); }
					Row.Namespace = *Namespace;
					Row.Key = *Key;
					Row.Source = *Source;
					Row.File = File;
				});
			}
		}
	}

	FString WriteDnaLocCsv(const TArray<FDnaLocRow>& Rows)
	{
		FString Out = TEXT("Namespace,Key,Source,Translation,Kind,Character,Dialogue,Previous,Tags,File,Line\n");
		for (const FDnaLocRow& Row : Rows)
		{
			const TArray<FString> Fields = { Row.Namespace, Row.Key, Row.Source, Row.Translation, Row.Kind, Row.Character, Row.Dialogue, Row.Previous, Row.Tags,
				Row.File, Row.Line > 0 ? FString::FromInt(Row.Line) : FString() };
			Out += FString::JoinBy(Fields, TEXT(","), [](const FString& Field) { return QuoteDnaCsvField(Field); });
			Out += TEXT("\n");
		}
		return Out;
	}

	bool ReadDnaLocCsv(const FString& Text, TArray<FDnaLocRow>& OutRows, FText& OutError)
	{
		OutRows.Reset();
		TArray<FString> Records;
		SplitRecords(Text.StartsWith(TEXT("﻿")) ? Text.Mid(1) : Text, Records);
		if (Records.Num() == 0)
		{
			OutError = LOCTEXT("EmptyCsv", "The file is empty");
			return false;
		}
		TArray<FString> Header;
		SplitDnaCsvRecord(Records[0], Header);
		auto Column = [&Header](const TCHAR* Name) { return Header.IndexOfByPredicate([Name](const FString& Each) { return Each.TrimStartAndEnd().Equals(Name, ESearchCase::IgnoreCase); }); };
		const int32 Namespace = Column(TEXT("Namespace")), Key = Column(TEXT("Key")), Source = Column(TEXT("Source")), Translation = Column(TEXT("Translation"));
		if (Namespace == INDEX_NONE || Key == INDEX_NONE || Source == INDEX_NONE || Translation == INDEX_NONE)
		{
			OutError = LOCTEXT("NoColumns", "The first row has to name the Namespace, Key, Source and Translation columns");
			return false;
		}
		const int32 Kind = Column(TEXT("Kind")), Character = Column(TEXT("Character")), Dialogue = Column(TEXT("Dialogue"));
		for (int32 Index = 1; Index < Records.Num(); ++Index)
		{
			TArray<FString> Fields;
			SplitDnaCsvRecord(Records[Index], Fields);
			auto Field = [&Fields](int32 At) { return Fields.IsValidIndex(At) ? Fields[At] : FString(); };
			FDnaLocRow& Row = OutRows.AddDefaulted_GetRef();
			Row.Namespace = Field(Namespace);
			Row.Key = Field(Key);
			Row.Source = Field(Source);
			Row.Translation = Field(Translation);
			Row.Kind = Field(Kind);
			Row.Character = Field(Character);
			Row.Dialogue = Field(Dialogue);
		}
		return true;
	}

	FDnaLocTarget GetDnaLocTarget(const FString& Name)
	{
		FDnaLocTarget Target;
		Target.Name = Name;
		Target.Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("Localization") / Name);
		return Target;
	}

	int32 FillDnaLocTranslations(const FDnaLocTarget& Target, const FString& Culture, TArray<FDnaLocRow>& Rows)
	{
		int32 Filled = 0;
#if WITH_EDITOR
		FText Error;
		const TSharedPtr<FLocTextHelper> Helper = OpenTarget(Target, Culture, Error);
		if (!Helper)
		{
			return 0;
		}
		for (FDnaLocRow& Row : Rows)
		{
			const TSharedPtr<FManifestEntry> Entry = Helper->FindSourceText(FLocKey(Row.Namespace), FLocKey(Row.Key));
			const FManifestContext* Context = Entry ? Entry->FindContextByKey(FLocKey(Row.Key)) : nullptr;
			if (!Context || !Entry->Source.Text.Equals(Row.Source, ESearchCase::CaseSensitive))
			{
				continue;
			}
			FLocItem Source, Translation;
			Helper->GetExportText(Culture, FLocKey(Row.Namespace), FLocKey(Row.Key), Context->KeyMetadataObj, ELocTextExportSourceMethod::NativeText, Entry->Source, Source, Translation);
			if (!Translation.Text.IsEmpty())
			{
				Row.Translation = Translation.Text;
				++Filled;
			}
		}
#endif
		return Filled;
	}

	bool ImportDnaLocTranslations(const FDnaLocTarget& Target, const FString& Culture, const TArray<FDnaLocRow>& Rows, FDnaLocImport& OutImport, FText& OutError)
	{
		OutImport = FDnaLocImport();
#if WITH_EDITOR
		const TSharedPtr<FLocTextHelper> Helper = OpenTarget(Target, Culture, OutError);
		if (!Helper)
		{
			return false;
		}
		for (const FDnaLocRow& Row : Rows)
		{
			if (Row.Translation.IsEmpty())
			{
				++OutImport.Empty;
				continue;
			}
			const FLocKey Namespace(Row.Namespace), Key(Row.Key);
			const TSharedPtr<FManifestEntry> Entry = Helper->FindSourceText(Namespace, Key);
			const FManifestContext* Context = Entry ? Entry->FindContextByKey(Key) : nullptr;
			if (!Context)
			{
				OutImport.Skipped.Add(FString::Printf(TEXT("%s %s: %s has not gathered it"), *Row.Namespace, *Row.Key, *Target.Name));
				continue;
			}
			if (!Entry->Source.Text.Equals(Row.Source, ESearchCase::CaseSensitive))
			{
				OutImport.Skipped.Add(FString::Printf(TEXT("%s %s: the source is now \"%s\", not \"%s\""), *Row.Namespace, *Row.Key, *Entry->Source.Text, *Row.Source));
				continue;
			}
			// Keyed on what the archive keys it on: the native culture's text where it has its own, as the dashboard's import does.
			FLocItem Source, Current;
			Helper->GetExportText(Culture, Namespace, Key, Context->KeyMetadataObj, ELocTextExportSourceMethod::NativeText, Entry->Source, Source, Current);
			const TSharedPtr<FArchiveEntry> Found = Helper->FindTranslation(Culture, Namespace, Key, Context->KeyMetadataObj);
			if (Found && Found->Source.IsExactMatch(Source) && Found->Translation.Text.Equals(Row.Translation, ESearchCase::CaseSensitive))
			{
				++OutImport.Unchanged;
				continue;
			}
			if (!Helper->ImportTranslation(Culture, Namespace, Key, Context->KeyMetadataObj, Source, FLocItem(Row.Translation), Context->bIsOptional))
			{
				OutImport.Skipped.Add(FString::Printf(TEXT("%s %s: the archive took no translation"), *Row.Namespace, *Row.Key));
				continue;
			}
			++OutImport.Imported;
		}
		return OutImport.Imported == 0 || Helper->SaveArchive(Culture, &OutError);
#else
		OutError = LOCTEXT("EditorOnly", "Translations are imported in the editor");
		return false;
#endif
	}
}

#undef LOCTEXT_NAMESPACE
