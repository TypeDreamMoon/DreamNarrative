// Copyright DreamMoon. All Rights Reserved.
//
//   DreamNarrative.Spec.<path>.<test name>   one automation test per `test` block of the suite
//   DreamNarrative.Spec.Coverage             every block again, under the coverage trace, against the
//                                            matrix in Tests/Spec/coverage.json

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Spec/DnaCoverage.h"
#include "Spec/DnaSpecSuite.h"

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FDnaSpecTest, "DreamNarrative.Spec", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

void FDnaSpecTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	using namespace UE::DreamNarrative::Tests::Spec;
	TArray<FString> Files;
	FindTestFiles(Files);
	for (const FString& Relative : Files)
	{
		for (const FString& Name : ScanTestNames(Relative))
		{
			OutBeautifiedNames.Add(ToDisplayPath(Relative) + TEXT(".") + Name);
			OutTestCommands.Add(Relative + TEXT("|") + Name);
		}
	}
}

bool FDnaSpecTest::RunTest(const FString& Parameters)
{
	FString Relative, BlockName;
	if (!Parameters.Split(TEXT("|"), &Relative, &BlockName))
	{
		AddError(FString::Printf(TEXT("Malformed test command '%s'"), *Parameters));
		return false;
	}
	return UE::DreamNarrative::Tests::Spec::RunSpecTest(*this, Relative, BlockName) && !HasAnyErrors();
}

// ---------------------------------------------------------------- coverage

namespace UE::DreamNarrative::Tests::Spec
{
	/** One category of coverage.json: cell -> what it means (or why it is not required). */
	bool ReadCells(const TSharedPtr<FJsonObject>& Root, const TCHAR* Category, TMap<FString, FString>& Out)
	{
		const TSharedPtr<FJsonObject>* Object = nullptr;
		if (!Root->TryGetObjectField(Category, Object) || !Object)
		{
			return false;
		}
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Object)->Values)
		{
			Out.Add(Pair.Key, Pair.Value.IsValid() ? Pair.Value->AsString() : FString());
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaSpecCoverageTest, "DreamNarrative.Spec.Coverage", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDnaSpecCoverageTest::RunTest(const FString& Parameters)
{
	using namespace UE::DreamNarrative::Tests;
	using namespace UE::DreamNarrative::Tests::Spec;

	// -- the matrix
	const FString MatrixPath = GetTestsDir() / TEXT("Spec/coverage.json");
	FString MatrixText;
	TSharedPtr<FJsonObject> Root;
	if (!FFileHelper::LoadFileToString(MatrixText, *MatrixPath)
		|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(MatrixText), Root) || !Root.IsValid())
	{
		AddError(FString::Printf(TEXT("Cannot read the coverage matrix %s"), *MatrixPath));
		return false;
	}
	TMap<FString, FString> Required, Pending, Outside;
	if (!ReadCells(Root, TEXT("required"), Required))
	{
		AddError(TEXT("coverage.json has no \"required\" object"));
		return false;
	}
	ReadCells(Root, TEXT("pending"), Pending);
	ReadCells(Root, TEXT("outside"), Outside);

	// -- every block of the suite, under the trace
	FDnaCoverage Coverage;
	int32 Blocks = 0;
	TArray<FString> Files;
	FindTestFiles(Files);
	for (const FString& Relative : Files)
	{
		for (const FString& Name : ScanTestNames(Relative))
		{
			RunSpecTest(*this, Relative, Name, &Coverage);
			++Blocks;
		}
	}
	const TMap<FString, int32>& Hits = Coverage.GetHits();

	// -- compare
	TArray<FString> Missing;
	for (const TPair<FString, FString>& Cell : Required)
	{
		if (!Hits.Contains(Cell.Key))
		{
			Missing.Add(FString::Printf(TEXT("%s -- %s"), *Cell.Key, *Cell.Value));
		}
	}
	TArray<FString> Unlisted;
	for (const TPair<FString, int32>& Hit : Hits)
	{
		if (!Required.Contains(Hit.Key) && !Pending.Contains(Hit.Key) && !Outside.Contains(Hit.Key))
		{
			Unlisted.Add(Hit.Key);
		}
	}
	Missing.Sort();
	Unlisted.Sort();
	for (const FString& Cell : Missing)
	{
		AddError(FString::Printf(TEXT("Not covered by any test block: %s"), *Cell));
	}
	for (const FString& Cell : Unlisted)
	{
		AddError(FString::Printf(TEXT("The runtime reported %s, which coverage.json does not list; add it (required, pending or outside)"), *Cell));
	}

	// -- the report
	TArray<FString> Report = { TEXT("# DreamNarrative spec coverage"), TEXT(""),
		FString::Printf(TEXT("%d test blocks; %d of %d required cells covered; %d pending; %d outside the suite."),
			Blocks, Required.Num() - Missing.Num(), Required.Num(), Pending.Num(), Outside.Num()),
		TEXT(""), TEXT("| Cell | Category | Hits |"), TEXT("|---|---|---|") };
	auto AddRows = [&Report, &Hits](const TMap<FString, FString>& Cells, const TCHAR* Category)
	{
		TArray<FString> Keys;
		Cells.GetKeys(Keys);
		Keys.Sort();
		for (const FString& Key : Keys)
		{
			const int32* Count = Hits.Find(Key);
			Report.Add(FString::Printf(TEXT("| `%s` | %s | %d |"), *Key, Category, Count ? *Count : 0));
		}
	};
	AddRows(Required, TEXT("required"));
	AddRows(Pending, TEXT("pending"));
	AddRows(Outside, TEXT("outside"));
	const FString ReportPath = FPaths::ProjectSavedDir() / TEXT("DreamNarrativeCoverage.md");
	FFileHelper::SaveStringToFile(FString::Join(Report, TEXT("\n")) + TEXT("\n"), *ReportPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	AddInfo(FString::Printf(TEXT("%s (report: %s)"), *Report[2], *FPaths::ConvertRelativePathToFull(ReportPath)));
	return Missing.Num() == 0 && Unlisted.Num() == 0;
}

#endif
