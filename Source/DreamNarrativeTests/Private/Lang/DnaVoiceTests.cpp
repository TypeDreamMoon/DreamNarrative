// Copyright DreamMoon. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AssetRegistry/AssetRegistryModule.h"
#include "DreamNarrativeDialogue.h"
#include "DreamNarrativeScript.h"
#include "DreamNarrativeSettings.h"
#include "DreamNarrativeVoice.h"
#include "Engine/DataTable.h"
#include "Generation/DnaGenerator.h"
#include "Lang/DnaProject.h"
#include "Lang/DnaVoice.h"
#include "Misc/Guid.h"
#include "Misc/ScopeExit.h"
#include "Sound/SoundWave.h"
#include "UObject/Package.h"

using namespace UE::DreamNarrative::Lang;

namespace
{
	/** Sound waves the asset registry knows, made in memory under one root, with the lengths given. */
	struct FVoiceAssets
	{
		FString Root = TEXT("/Temp/DnaVoice_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
		TArray<UObject*> Made;

		FString Add(const FString& Relative, float Seconds)
		{
			const FString PackageName = Root / Relative;
			const FString Name = FPackageName::GetShortName(PackageName);
			USoundWave* Wave = NewObject<USoundWave>(CreatePackage(*PackageName), *Name, RF_Public | RF_Standalone);
			Wave->Duration = Seconds;
			FAssetRegistryModule::AssetCreated(Wave);
			Made.Add(Wave);
			return Wave->GetPathName();
		}

		~FVoiceAssets()
		{
			for (UObject* Object : Made)
			{
				FAssetRegistryModule::AssetDeleted(Object);
				Object->ClearFlags(RF_Standalone);
			}
		}
	};

	struct FVoiceRun
	{
		TArray<UDreamNarrativeScript*> Scripts;
		FDnaDiagnosticSink Diagnostics;

		const FDnaLineData* Line(const FString& Id) const
		{
			for (const UDreamNarrativeScript* Script : Scripts)
			{
				const UDnaDialogueSection* Section = Script->FindSection<UDnaDialogueSection>();
				for (const FDnaDialogueNodeData& Node : Section ? Section->Nodes : TArray<FDnaDialogueNodeData>())
				{
					for (const FDnaLineData& Each : Node.Lines) { if (Each.Id.ToString() == Id) { return &Each; } }
				}
			}
			return nullptr;
		}

		TArray<int32> LinesWith(const TCHAR* Code) const
		{
			TArray<int32> Out;
			for (const FDnaDiagnostic& Each : Diagnostics.GetDiagnostics()) { if (Each.Code == Code) { Out.Add(Each.Span.Line); } }
			return Out;
		}
	};

	void Generate(const FString& Source, const TMap<FString, FString>& Csv, bool bDataTables, FVoiceRun& Out)
	{
		const FString Path = TEXT("Voices.dna");
		const TMap<FString, FString> Texts = { { Path, Source } };
		FDnaProgram Program;
		LoadDnaProgram({ Path }, Program, Out.Diagnostics, &Texts);
		FDnaGenerateOptions Options;
		Options.bTransient = true;
		Options.bSave = false;
		Options.VoiceCsvTexts = &Csv;
		Options.bReadVoiceDataTables = bDataTables;
		Options.bReportVoiceOrphans = true;
		FDnaGenerateResult Result;
		GenerateDnaProgram(Program, Options, Result, Out.Diagnostics);
		Out.Scripts = Result.Scripts;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaVoiceLookupTest, "DreamNarrative.Lang.VoiceLookup", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDnaVoiceLookupTest::RunTest(const FString& Parameters)
{
	UDreamNarrativeSettings* Settings = GetMutableDefault<UDreamNarrativeSettings>();
	const EDnaVoiceLookup Lookup = Settings->DefaultVoiceLookup;
	const TArray<FSoftObjectPath> Tables = Settings->VoiceTables;
	ON_SCOPE_EXIT { Settings->DefaultVoiceLookup = Lookup; Settings->VoiceTables = Tables; };
	Settings->DefaultVoiceLookup = EDnaVoiceLookup::None;
	Settings->VoiceTables.Reset();

	FVoiceAssets Assets;
	const FString ByConvention = Assets.Add(TEXT("老周/老周_初见_1"), 2.5f);
	const FString Own = Assets.Add(TEXT("Own/VO_特殊版"), 1.25f);
	const FString FromCsv = Assets.Add(TEXT("Table/XW_003"), 3.f);
	const FString Source = FString::Printf(TEXT(
		"character 老周\n"
		"  voice: %s/老周\n"
		"character 小雾\n"
		"  voice: table\n"
		"character 路人\n"
		"dialogue 老周_初见 for 老周\n"
		"  老周: 一。\n"
		"  老周: 二。 #老周_二 voice %s\n"
		"  小雾: 三。 #小雾_3\n"
		"  小雾: 四。 #小雾_4\n"
		"  路人: 五。\n"), *Assets.Root, *Own);
	const TMap<FString, FString> Csv = { { TEXT("Lines.voice.csv"), FString::Printf(TEXT(
		"LineId,Voice,Text\n"
		"小雾_3,%s,三。\n"
		"小雾_4,,四。\n"
		"没有这一行,%s,\n"), *FromCsv, *FromCsv) } };

	FVoiceRun Run;
	Generate(Source, Csv, /*bDataTables=*/false, Run);
	if (!TestEqual(TEXT("one script"), Run.Scripts.Num(), 1)) { return false; }
	const FDnaLineData* First = Run.Line(TEXT("老周_初见.1"));
	if (TestNotNull(TEXT("convention line"), First))
	{
		TestEqual(TEXT("the convention finds <root>/<id, dots as underscores>"), First->Voice.ToSoftObjectPath().ToString(), ByConvention);
		TestEqual(TEXT("with its length"), First->VoiceSeconds, 2.5f);
	}
	const FDnaLineData* Second = Run.Line(TEXT("老周_二"));
	TestTrue(TEXT("a line's own voice wins"), Second && Second->Voice.ToSoftObjectPath().ToString() == Own && FMath::IsNearlyEqual(Second->VoiceSeconds, 1.25f));
	const FDnaLineData* Third = Run.Line(TEXT("小雾_3"));
	TestTrue(TEXT("a table character reads the table"), Third && Third->Voice.ToSoftObjectPath().ToString() == FromCsv);
	const FDnaLineData* Fourth = Run.Line(TEXT("小雾_4"));
	TestTrue(TEXT("a row with no voice is not recorded yet"), Fourth && Fourth->Voice.IsNull());
	const FDnaLineData* Fifth = Run.Line(TEXT("老周_初见.5"));
	TestTrue(TEXT("a character with no lookup has none, by default"), Fifth && Fifth->Voice.IsNull());
	TestEqual(TEXT("the missing voice is reported where the line is"), Run.LinesWith(TEXT("DNS6003")), TArray<int32>{ 10 });
	TestEqual(TEXT("a row no line answers is an orphan"), Run.LinesWith(TEXT("DNS6004")), TArray<int32>{ 4 });
	TestEqual(TEXT("the script refers to its voices"), Run.Scripts[0]->AssetReferences.Num(), 3);
	TestFalse(TEXT("warnings only"), Run.Diagnostics.HasErrors());

	// A project default of the convention reaches the speaker with no root of its own: <default root>/<角色>/<id>.
	Settings->DefaultVoiceLookup = EDnaVoiceLookup::Convention;
	FVoiceRun Defaulted;
	Generate(Source, Csv, false, Defaulted);
	TestEqual(TEXT("now the passer-by is looked for too"), Defaulted.LinesWith(TEXT("DNS6003")), TArray<int32>{ 10, 11 });
	Settings->DefaultVoiceLookup = EDnaVoiceLookup::None;

	// A DataTable answers too; the same line in a table and a CSV is one line with two voices.
	UDataTable* Table = NewObject<UDataTable>(CreatePackage(*(Assets.Root / TEXT("DT_Voices"))), TEXT("DT_Voices"), RF_Public | RF_Standalone);
	Table->RowStruct = FDnaVoiceRow::StaticStruct();
	FDnaVoiceRow Row;
	Row.Voice = TSoftObjectPtr<USoundBase>(FSoftObjectPath(FromCsv));
	Table->AddRow(TEXT("小雾_4"), Row);
	Settings->VoiceTables = { FSoftObjectPath(Table) };
	ON_SCOPE_EXIT { Table->ClearFlags(RF_Standalone); };
	FVoiceRun FromTable;
	Generate(Source, Csv, /*bDataTables=*/true, FromTable);
	const FDnaLineData* Fourth2 = FromTable.Line(TEXT("小雾_4"));
	TestTrue(TEXT("the DataTable answers the line"), Fourth2 && Fourth2->Voice.ToSoftObjectPath().ToString() == FromCsv);
	TestEqual(TEXT("nothing missing now"), FromTable.LinesWith(TEXT("DNS6003")).Num(), 0);

	Table->AddRow(TEXT("小雾_3"), Row);
	FVoiceRun Twice;
	Generate(Source, Csv, true, Twice);
	TestEqual(TEXT("one line answered twice is an error"), Twice.LinesWith(TEXT("DNS6005")).Num(), 1);
	TestTrue(TEXT("and stops the build"), Twice.Diagnostics.HasErrors());
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaVoiceCsvTest, "DreamNarrative.Lang.VoiceCsv", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDnaVoiceCsvTest::RunTest(const FString& Parameters)
{
	TArray<FString> Fields;
	SplitDnaCsvRecord(TEXT("a, \"b, c\" ,\"say \"\"hi\"\"\",,"), Fields);
	TestEqual(TEXT("quotes, commas, doubled quotes, empty fields"), Fields, TArray<FString>{ TEXT("a"), TEXT("b, c"), TEXT("say \"hi\""), TEXT(""), TEXT("") });
	TestEqual(TEXT("quoting round-trips"), QuoteDnaCsvField(TEXT("say \"hi\", then go")), FString(TEXT("\"say \"\"hi\"\", then go\"")));
	TestEqual(TEXT("dots become underscores"), GetDnaVoiceAssetName(TEXT("老周_初见.c2")), FString(TEXT("老周_初见_c2")));
	TMap<FString, FDnaVoiceEntry> Entries;
	FDnaDiagnosticSink Diagnostics;
	ReadDnaVoiceTables({ { TEXT("Bad.voice.csv"), TEXT("Id,Sound\nx,/Game/x\n") } }, false, Entries, Diagnostics);
	TestTrue(TEXT("a table without the two columns is refused"), Diagnostics.HasErrors() && Entries.Num() == 0);
	return !HasAnyErrors();
}

#endif
