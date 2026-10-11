// Copyright DreamMoon. All Rights Reserved.

#include "Lang/DnaVoice.h"
#include "DreamNarrativeSettings.h"
#include "DreamNarrativeVoice.h"
#include "DreamNarrativeDialogue.h"
#include "DreamNarrativeScript.h"
#include "Lang/DnaModel.h"
#include "Lang/DnaProject.h"
#include "Engine/DataTable.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Algo/StableSort.h"

#define LOCTEXT_NAMESPACE "DnaVoice"

namespace UE::DreamNarrative::Lang
{
	void FindDnaVoiceCsvFiles(const FString& Root, TArray<FString>& OutFiles)
	{
		OutFiles.Reset();
		if (!IFileManager::Get().DirectoryExists(*Root))
		{
			return;
		}
		IFileManager::Get().FindFilesRecursive(OutFiles, *Root, TEXT("*.voice.csv"), true, false, false);
		for (FString& File : OutFiles)
		{
			File = FPaths::ConvertRelativePathToFull(File);
		}
		OutFiles.Sort();
	}

	FString GetDnaVoiceAssetName(const FString& LineId)
	{
		return LineId.Replace(TEXT("."), TEXT("_"));
	}

	void SplitDnaCsvRecord(const FString& Record, TArray<FString>& OutFields)
	{
		OutFields.Reset();
		FString Field;
		bool bQuoted = false;
		for (int32 Index = 0; Index < Record.Len(); ++Index)
		{
			const TCHAR Char = Record[Index];
			if (bQuoted)
			{
				if (Char == TEXT('"'))
				{
					if (Index + 1 < Record.Len() && Record[Index + 1] == TEXT('"')) { Field.AppendChar(TEXT('"')); ++Index; }
					else { bQuoted = false; }
				}
				else
				{
					Field.AppendChar(Char);
				}
			}
			else if (Char == TEXT('"'))
			{
				bQuoted = true;
			}
			else if (Char == TEXT(','))
			{
				OutFields.Add(Field.TrimStartAndEnd());
				Field.Reset();
			}
			else
			{
				Field.AppendChar(Char);
			}
		}
		OutFields.Add(Field.TrimStartAndEnd());
	}

	FString QuoteDnaCsvField(const FString& Field)
	{
		if (!Field.Contains(TEXT(",")) && !Field.Contains(TEXT("\"")) && !Field.Contains(TEXT("\n")) && !Field.Contains(TEXT("\r")))
		{
			return Field;
		}
		return TEXT("\"") + Field.Replace(TEXT("\""), TEXT("\"\"")) + TEXT("\"");
	}

	namespace
	{
		void Add(TMap<FString, FDnaVoiceEntry>& Entries, const FString& LineId, FDnaVoiceEntry&& Entry, FDnaDiagnosticSink& Diagnostics)
		{
			if (const FDnaVoiceEntry* Earlier = Entries.Find(LineId))
			{
				FDnaSpan Span;
				Span.Line = FMath::Max(1, Entry.Line);
				Diagnostics.SetFilePath(Entry.Source);
				Diagnostics.Error(TEXT("DNS6005"), Span, FText::Format(LOCTEXT("VoiceTwice",
					"Line '{0}' already has a voice in {1}{2}; one line, one voice"),
					FText::FromString(LineId), FText::FromString(Earlier->Source),
					Earlier->Line > 0 ? FText::Format(LOCTEXT("AtRow", " (row {0})"), FText::AsNumber(Earlier->Line)) : FText::GetEmpty()));
				return;
			}
			Entries.Add(LineId, MoveTemp(Entry));
		}

		void ReadCsv(const FString& Path, const FString& Text, TMap<FString, FDnaVoiceEntry>& Entries, FDnaDiagnosticSink& Diagnostics)
		{
			TArray<FString> Records;
			Text.ParseIntoArrayLines(Records, /*bCullEmpty=*/false);
			int32 IdColumn = INDEX_NONE;
			int32 VoiceColumn = INDEX_NONE;
			int32 HeaderRow = 0;
			TArray<FString> Fields;
			for (int32 Row = 0; Row < Records.Num(); ++Row)
			{
				if (Records[Row].TrimStartAndEnd().IsEmpty()) { continue; }
				SplitDnaCsvRecord(Records[Row], Fields);
				if (HeaderRow == 0)
				{
					HeaderRow = Row + 1;
					for (int32 Column = 0; Column < Fields.Num(); ++Column)
					{
						if (Fields[Column].Equals(TEXT("LineId"), ESearchCase::IgnoreCase)) { IdColumn = Column; }
						if (Fields[Column].Equals(TEXT("Voice"), ESearchCase::IgnoreCase)) { VoiceColumn = Column; }
					}
					if (IdColumn == INDEX_NONE || VoiceColumn == INDEX_NONE)
					{
						FDnaSpan Span;
						Span.Line = HeaderRow;
						Diagnostics.SetFilePath(Path);
						Diagnostics.Error(TEXT("DNS9002"), Span, LOCTEXT("VoiceHeader", "A voice table needs a header row with the columns LineId and Voice"));
						return;
					}
					continue;
				}
				const FString LineId = Fields.IsValidIndex(IdColumn) ? Fields[IdColumn] : FString();
				const FString Voice = Fields.IsValidIndex(VoiceColumn) ? Fields[VoiceColumn] : FString();
				if (LineId.IsEmpty() || Voice.IsEmpty()) { continue; }
				Add(Entries, LineId, FDnaVoiceEntry{ Voice, Path, Row + 1 }, Diagnostics);
			}
		}
	}

	void ReadDnaVoiceTables(const TMap<FString, FString>& CsvTexts, bool bDataTables, TMap<FString, FDnaVoiceEntry>& OutEntries, FDnaDiagnosticSink& Diagnostics)
	{
		const FString FilePath = Diagnostics.GetFilePath();
		TArray<FString> Paths;
		CsvTexts.GetKeys(Paths);
		Paths.Sort();
		for (const FString& Path : Paths)
		{
			ReadCsv(Path, CsvTexts[Path], OutEntries, Diagnostics);
		}
		if (bDataTables)
		{
			for (const FSoftObjectPath& TablePath : UDreamNarrativeSettings::Get()->VoiceTables)
			{
				const UDataTable* Table = Cast<UDataTable>(TablePath.TryLoad());
				if (!Table || !Table->GetRowStruct() || !Table->GetRowStruct()->IsChildOf(FDnaVoiceRow::StaticStruct()))
				{
					Diagnostics.SetFilePath(TablePath.ToString());
					Diagnostics.Error(TEXT("DNS9002"), FDnaSpan(), FText::Format(LOCTEXT("VoiceDataTable",
						"Voice table {0} cannot be read: it has to be a DataTable of FDnaVoiceRow"), FText::FromString(TablePath.ToString())));
					continue;
				}
				Table->ForeachRow<FDnaVoiceRow>(TEXT("ReadDnaVoiceTables"), [&](const FName& Key, const FDnaVoiceRow& Row)
				{
					if (!Row.Voice.IsNull())
					{
						Add(OutEntries, Key.ToString(), FDnaVoiceEntry{ Row.Voice.ToSoftObjectPath().ToString(), TablePath.ToString(), 0 }, Diagnostics);
					}
				});
			}
		}
		Diagnostics.SetFilePath(FilePath);
	}

	EDnaVoiceLookup GetDnaVoiceLookup(const FDnaProgram& Program, FName Speaker, FString& OutRoot)
	{
		const UDreamNarrativeSettings* Settings = UDreamNarrativeSettings::Get();
		OutRoot = Settings->DefaultVoiceRoot;
		OutRoot.RemoveFromEnd(TEXT("/"));
		if (Speaker.IsNone())
		{
			return Settings->DefaultVoiceLookup;
		}
		OutRoot /= Speaker.ToString();
		const FDnaCharacterInfo* Info = Program.Characters.Find(Speaker.ToString());
		if (Info && Info->VoiceRoot == TEXT("table"))
		{
			return EDnaVoiceLookup::Table;
		}
		if (Info && !Info->VoiceRoot.IsEmpty())
		{
			OutRoot = Info->VoiceRoot;
			OutRoot.RemoveFromEnd(TEXT("/"));
			return EDnaVoiceLookup::Convention;
		}
		return Settings->DefaultVoiceLookup;
	}

	void CollectDnaVoiceScript(const FDnaProgram& Program, const TArray<UDreamNarrativeScript*>& Scripts, TArray<FDnaVoiceScriptRow>& OutRows)
	{
		OutRows.Reset();
		const FString SourceRoot = GetDnaSourceRoot() + TEXT("/");
		for (const UDreamNarrativeScript* Script : Scripts)
		{
			const UDnaDialogueSection* Section = Script ? Script->FindSection<UDnaDialogueSection>() : nullptr;
			if (!Section) { continue; }
			FString Source = Script->SourcePath;
			FPaths::NormalizeFilename(Source);
			if (FPaths::IsUnderDirectory(Source, SourceRoot)) { FPaths::MakePathRelativeTo(Source, *SourceRoot); }
			for (const FDnaDialogueNodeData& Node : Section->Nodes)
			{
				FString Previous;
				for (const FDnaLineData& Line : Node.Lines)
				{
					FDnaVoiceScriptRow& Row = OutRows.AddDefaulted_GetRef();
					Row.LineId = Line.Id.ToString();
					Row.Voice = Line.Voice.IsNull() ? FString() : Line.Voice.ToSoftObjectPath().ToString();
					Row.Character = Line.Speaker.IsNone() ? FString() : Line.Speaker.ToString();
					Row.Text = Line.Text.ToString();
					Row.Dialogue = Node.Name.ToString();
					Row.Previous = Previous;
					Row.Tags = FString::JoinBy(Line.Tags, TEXT(" "), [](const FName& Tag) { return Tag.ToString(); });
					Row.For = Line.Duration;
					FString Root;
					const EDnaVoiceLookup Lookup = GetDnaVoiceLookup(Program, Line.Speaker, Root);
					Row.Status = !Row.Voice.IsEmpty() ? TEXT("found") : Lookup == EDnaVoiceLookup::None ? TEXT("none") : TEXT("missing");
					Row.Expected = Lookup == EDnaVoiceLookup::Convention ? Root / GetDnaVoiceAssetName(Row.LineId) : FString();
					Row.Source = Source;
					Row.SourceLine = Line.SourceLine;
					Previous = Row.Text;
				}
			}
		}
		// By character (narration last), and within one in the order the dialogues have them.
		Algo::StableSortBy(OutRows, [](const FDnaVoiceScriptRow& Row) { return Row.Character.IsEmpty() ? FString(TEXT("\uFFFF")) : Row.Character; });
	}

	FString WriteDnaVoiceScript(const TArray<FDnaVoiceScriptRow>& Rows)
	{
		FString Out = TEXT("LineId,Voice,Character,Text,Dialogue,Previous,Tags,For,Status,Expected,Source,SourceLine\n");
		for (const FDnaVoiceScriptRow& Row : Rows)
		{
			const TArray<FString> Fields = { Row.LineId, Row.Voice, Row.Character, Row.Text, Row.Dialogue, Row.Previous, Row.Tags,
				Row.For > 0.f ? FString::SanitizeFloat(Row.For) : FString(), Row.Status, Row.Expected, Row.Source, FString::FromInt(Row.SourceLine) };
			Out += FString::JoinBy(Fields, TEXT(","), [](const FString& Field) { return QuoteDnaCsvField(Field); });
			Out += TEXT("\n");
		}
		return Out;
	}
}

#undef LOCTEXT_NAMESPACE
