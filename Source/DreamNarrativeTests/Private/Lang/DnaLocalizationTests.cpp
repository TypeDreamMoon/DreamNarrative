// Copyright DreamMoon. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DreamNarrativeDialogueRunner.h"
#include "DreamNarrativeLedger.h"
#include "DreamNarrativeScript.h"
#include "DreamNarrativeStory.h"
#include "Fixtures/DnaGameFixture.h"
#include "Fixtures/DnaTestWorld.h"
#include "HAL/FileManager.h"
#include "Internationalization/Culture.h"
#include "Internationalization/GatherableTextData.h"
#include "Internationalization/Internationalization.h"
#include "Internationalization/PolyglotTextData.h"
#include "Internationalization/TextLocalizationManager.h"
#include "Dom/JsonObject.h"
#include "HAL/PlatformProcess.h"
#include "Lang/DnaLocalization.h"
#include "Lang/DnaProject.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Serialization/PropertyLocalizationDataGathering.h"
#include "UObject/UObjectIterator.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaLocalizationTest, "DreamNarrative.Lang.Localization", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDnaLocalizationTest::RunTest(const FString& Parameters)
{
	using namespace UE::DreamNarrative::Tests;

	const FString Sample = TEXT("Tests/Corpus/Validate/Samples/automata.dna");
	const FString Namespace = TEXT("DNA/automata");

	// -- gathering: the script packages hold their texts the way the engine's gatherer collects them,
	// under the file's namespace and keys that do not depend on where a line sits
	TArray<FGatherableTextData> Gathered;
	{
		FDnaScratchAssets Assets(*this, { Sample });
		if (!Assets.IsValid())
		{
			return false;
		}
		for (TObjectIterator<UDreamNarrativeScript> It; It; ++It)
		{
			if (It->GetPackage()->GetName().StartsWith(Assets.GetDirectory()))
			{
				EPropertyLocalizationGathererResultFlags Flags = EPropertyLocalizationGathererResultFlags::Empty;
				FPropertyLocalizationDataGatherer(Gathered, It->GetPackage(), Flags);
			}
		}
	}
	auto KeyOf = [&Gathered, &Namespace](const FString& Source, const FString& Key = FString()) -> FString
	{
		for (const FGatherableTextData& Data : Gathered)
		{
			if (Data.NamespaceName != Namespace || Data.SourceData.SourceString != Source)
			{
				continue;
			}
			for (const FTextSourceSiteContext& Site : Data.SourceSiteContexts)
			{
				if (Key.IsEmpty() || Site.KeyName == Key)
				{
					return Site.KeyName;
				}
			}
		}
		return FString();
	};
	const FString LineKey = KeyOf(TEXT("结束了吗……?"));
	TestFalse(TEXT("a line of dialogue is gathered"), LineKey.IsEmpty());
	TestFalse(TEXT("a choice"), KeyOf(TEXT("别大意。")).IsEmpty());
	TestFalse(TEXT("a quest's title"), KeyOf(TEXT("迷途的机械")).IsEmpty());
	TestFalse(TEXT("a journal line"), KeyOf(TEXT("有台机械说想吃鱼。")).IsEmpty());
	TestFalse(TEXT("a stage's text, under its stage"), KeyOf(TEXT("这里也有……一样的东西"), TEXT("stage.骇入闪回.text0")).IsEmpty());

	// -- a translation: registered for the line, the dialogue shows it in that language, and the source again after
	FDnaTestWorld World(*this);
	if (!World.CompilePluginFiles({ Sample }) || LineKey.IsEmpty())
	{
		return false;
	}
	FDnaEventPayload Defeated;
	Defeated.Event = TEXT("Boss击破");
	World.Ledger().RaiseEvent(Defeated);
	UDreamNarrativeDialogueRunner* Runner = World.Story().GetActiveRunner();
	if (!TestNotNull(TEXT("the dialogue after the boss runs"), Runner))
	{
		return false;
	}
	TestEqual(TEXT("in the source language"), Runner->GetCurrentLine().Text.ToString(), FString(TEXT("结束了吗……?")));

	// A translation for the line's namespace and key. A game ships it in its .locres; here it is registered
	// in memory, under a category that follows the current language even in the editor (game text there
	// follows the project's preview language setting instead).
	const FString Previous = FInternationalization::Get().GetCurrentLanguage()->GetName();
	FPolyglotTextData Translation(ELocalizedTextSourceCategory::Engine, Namespace, LineKey, TEXT("结束了吗……?"));
	Translation.AddLocalizedString(TEXT("fr"), TEXT("C'est fini... ?"));
	FTextLocalizationManager::Get().RegisterPolyglotTextData(Translation);
	if (TestTrue(TEXT("switch to French"), FInternationalization::Get().SetCurrentLanguage(TEXT("fr"))))
	{
		TestEqual(TEXT("the line in French"), Runner->GetCurrentLine().Text.ToString(), FString(TEXT("C'est fini... ?")));
		FInternationalization::Get().SetCurrentLanguage(Previous);
	}
	TestEqual(TEXT("and back"), Runner->GetCurrentLine().Text.ToString(), FString(TEXT("结束了吗……?")));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaProjectPathTest, "DreamNarrative.Lang.ProjectPaths", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDnaProjectPathTest::RunTest(const FString& Parameters)
{
	using namespace UE::DreamNarrative::Lang;
	// With engine and project on one drive the engine's project directory is relative to its binaries;
	// joined twice it named a directory that is not there, and nothing under it was found.
	AddInfo(FString::Printf(TEXT("the engine's project directory: %s"), *FPaths::ProjectDir()));
	const FString Project = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	const FString Resolved = ResolveDnaProjectPath(TEXT("DNarrative/Ch01/灯塔.dna"));
	TestFalse(TEXT("a relative path comes out absolute"), FPaths::IsRelative(Resolved));
	TestTrue(TEXT("under the project, once"), FPaths::IsSamePath(Resolved, Project / TEXT("DNarrative/Ch01/灯塔.dna")));
	TestTrue(TEXT("an absolute path stays as it is"), FPaths::IsSamePath(ResolveDnaProjectPath(Project / TEXT("Elsewhere/甲.dna")), Project / TEXT("Elsewhere/甲.dna")));
	TestFalse(TEXT("the source directory is absolute"), FPaths::IsRelative(GetDnaSourceRoot()));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaLocManifestTest, "DreamNarrative.Lang.LocalizationManifest", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDnaLocManifestTest::RunTest(const FString& Parameters)
{
	using namespace UE::DreamNarrative::Lang;
	using namespace UE::DreamNarrative::Tests;

	const TArray<FDnaTestSource> Sources = { { TEXT("Manifest.dna"), TEXT(
		"character 老周\n"
		"  display: 老周头\n"
		"character 小林\n"
		"character 新人 was 旧人\n"
		"fact 金币: int\n"
		"chapter 序章 was 开头\n"
		"stage 字幕\n"
		"  duration 1.0s\n"
		"  ui\n"
		"    0.0s..0.5s   text 「灯亮了」 #灯亮\n"
		"    0.5s..1.0s   text 「再见」\n"
		"quest 修灯 was 补灯\n"
		"  objective 金币 >= 3\n"
		"    show: 攒够 {金币} 枚\n"
		"dialogue 新对话 was 旧对话 for 老周\n"
		"  /// 第一次见面,语气冷淡\n"
		"  /// 别翻成客套话\n"
		"  老周(皱眉): 你带了 {金币} 枚金币? for 2.5s\n"
		"  老周: 有 id 的一句。 #固定\n"
		"  - 走吧。\n"
		"      老周: 走。\n"
		"  // 普通注释不算\n"
		"  - 等等。 #等等\n") } };
	FDnaGenerateOptions Options;
	Options.bSave = false;
	Options.bTransient = true;
	FDnaProgram Program;
	TArray<UDreamNarrativeScript*> Scripts;
	TArray<FDnaLocManifest> Manifests;
	if (!CompileDnaSources(*this, Sources, Options, Program, Scripts, &Manifests) || !TestEqual(TEXT("one manifest a file"), Manifests.Num(), 1))
	{
		return false;
	}
	const FDnaLocManifest& Manifest = Manifests[0];
	TestEqual(TEXT("the file's namespace"), Manifest.Namespace, FString(TEXT("DNA/Manifest")));
	auto Find = [&Manifest](const TCHAR* Key) { return Manifest.Entries.FindByPredicate([Key](const FDnaLocEntry& Entry) { return Entry.Key == Key; }); };
	auto ContextOf = [](const FDnaLocEntry* Entry, const TCHAR* Name)
	{
		const TPair<FString, FString>* Found = Entry ? Entry->Context.FindByPredicate([Name](const TPair<FString, FString>& Pair) { return Pair.Key == Name; }) : nullptr;
		return Found ? Found->Value : FString();
	};

	// -- a line numbered by position: unstable, renamed with its dialogue, with what a translator needs
	const FDnaLocEntry* Line = Find(TEXT("新对话.1"));
	if (TestNotNull(TEXT("a numbered line"), Line))
	{
		TestEqual(TEXT("its source is the pattern"), Line->Source, FString(TEXT("你带了 {0} 枚金币?")));
		TestEqual(TEXT("a line"), Line->Kind, FString(TEXT("line")));
		TestEqual(TEXT("where it is"), Line->At, FString(TEXT("新对话#新对话.1")));
		TestFalse(TEXT("its key moves"), Line->bStable);
		TestEqual(TEXT("freeze writes its id"), Line->FreezeId, FString(TEXT("新对话.1")));
		TestEqual(TEXT("the dialogue was renamed"), Line->RenamedFrom, FString(TEXT("旧对话.1")));
		TestEqual(TEXT("the /// lines above it"), Line->Note, FString(TEXT("第一次见面,语气冷淡\n别翻成客套话")));
		TestEqual(TEXT("who says it"), ContextOf(Line, TEXT("speaker")), FString(TEXT("老周")));
		TestEqual(TEXT("as shown"), ContextOf(Line, TEXT("speakerDisplay")), FString(TEXT("老周头")));
		TestEqual(TEXT("in which dialogue"), ContextOf(Line, TEXT("dialogue")), FString(TEXT("新对话")));
		TestEqual(TEXT("how"), ContextOf(Line, TEXT("tags")), FString(TEXT("皱眉")));
		TestEqual(TEXT("for how long"), ContextOf(Line, TEXT("for")), FString(TEXT("2.5")));
		TestEqual(TEXT("what {0} is"), Line->Args, TArray<FString>{ TEXT("金币") });
		TestTrue(TEXT("nothing before it"), ContextOf(Line, TEXT("previous")).IsEmpty());
	}
	const FDnaLocEntry* Fixed = Find(TEXT("固定"));
	if (TestNotNull(TEXT("a line with an id"), Fixed))
	{
		TestTrue(TEXT("holds still"), Fixed->bStable && Fixed->FreezeId.IsEmpty() && Fixed->RenamedFrom.IsEmpty());
		TestEqual(TEXT("the line before it"), ContextOf(Fixed, TEXT("previous")), FString(TEXT("你带了 {0} 枚金币?")));
		TestTrue(TEXT("no note without ///"), Fixed->Note.IsEmpty());
	}
	const FDnaLocEntry* Go = Find(TEXT("新对话.c1"));
	const FDnaLocEntry* Wait = Find(TEXT("等等"));
	if (TestNotNull(TEXT("a numbered choice"), Go) && TestNotNull(TEXT("a choice with an id"), Wait))
	{
		TestEqual(TEXT("a choice"), Go->Kind, FString(TEXT("choice")));
		TestEqual(TEXT("renamed too"), Go->RenamedFrom, FString(TEXT("旧对话.c1")));
		TestEqual(TEXT("after the line before the group"), ContextOf(Go, TEXT("previous")), FString(TEXT("有 id 的一句。")));
		TestEqual(TEXT("so is the next option, not after the first one's body"), ContextOf(Wait, TEXT("previous")), FString(TEXT("有 id 的一句。")));
		TestTrue(TEXT("a plain comment is no note"), Wait->Note.IsEmpty());
	}

	// -- names shown as they are are translated too, renamed ones say so
	const FDnaLocEntry* Shown = Find(TEXT("character.老周"));
	const FDnaLocEntry* Plain = Find(TEXT("character.小林"));
	const FDnaLocEntry* Renamed = Find(TEXT("character.新人"));
	TestTrue(TEXT("a display name"), Shown && Shown->Source == TEXT("老周头"));
	TestTrue(TEXT("a name without one shows itself"), Plain && Plain->Source == TEXT("小林") && Plain->Kind == TEXT("character"));
	TestTrue(TEXT("a renamed character"), Renamed && Renamed->RenamedFrom == TEXT("character.旧人"));
	const FDnaLocEntry* Chapter = Find(TEXT("chapter.序章"));
	TestTrue(TEXT("a chapter without a title"), Chapter && Chapter->Source == TEXT("序章") && Chapter->RenamedFrom == TEXT("chapter.开头"));
	const FDnaLocEntry* Title = Find(TEXT("quest.修灯.title"));
	TestTrue(TEXT("a quest without a title"), Title && Title->Source == TEXT("修灯") && Title->RenamedFrom == TEXT("quest.补灯.title") && Title->bStable);
	const FDnaLocEntry* Objective = Find(TEXT("quest.修灯.objective1"));
	if (TestNotNull(TEXT("an objective"), Objective))
	{
		TestTrue(TEXT("numbered"), !Objective->bStable && Objective->FreezeId == TEXT("objective1"));
		TestEqual(TEXT("its pattern"), Objective->Source, FString(TEXT("攒够 {0} 枚")));
		TestEqual(TEXT("its argument"), Objective->Args, TArray<FString>{ TEXT("金币") });
		TestEqual(TEXT("renamed with the quest"), Objective->RenamedFrom, FString(TEXT("quest.补灯.objective1")));
	}
	const FDnaLocEntry* Named = Find(TEXT("stage.字幕.灯亮"));
	const FDnaLocEntry* Second = Find(TEXT("stage.字幕.text1"));
	TestTrue(TEXT("a stage's text with an id"), Named && Named->bStable && Named->At == TEXT("stage.字幕#灯亮"));
	TestTrue(TEXT("one without, numbered among all its texts"), Second && !Second->bStable && Second->Source == TEXT("再见"));

	// -- the JSON a translation tool reads
	const FString Json = WriteDnaLocManifest(Manifest, TEXT("DNarrative/Manifest.dna"));
	TSharedPtr<FJsonObject> Root;
	if (TestTrue(TEXT("it is JSON"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) && Root.IsValid()))
	{
		TestEqual(TEXT("version"), static_cast<int32>(Root->GetNumberField(TEXT("version"))), 1);
		TestEqual(TEXT("provider"), Root->GetStringField(TEXT("provider")), FString(TEXT("DreamNarrative")));
		TestEqual(TEXT("source"), Root->GetStringField(TEXT("source")), FString(TEXT("DNarrative/Manifest.dna")));
		TestEqual(TEXT("namespace"), Root->GetStringField(TEXT("namespace")), FString(TEXT("DNA/Manifest")));
		const TArray<TSharedPtr<FJsonValue>>& Entries = Root->GetArrayField(TEXT("entries"));
		TestEqual(TEXT("every entry"), Entries.Num(), Manifest.Entries.Num());
		const TSharedPtr<FJsonValue>* First = Entries.FindByPredicate([](const TSharedPtr<FJsonValue>& Value) { return Value->AsObject()->GetStringField(TEXT("key")) == TEXT("新对话.1"); });
		if (TestNotNull(TEXT("the line"), First))
		{
			const TSharedPtr<FJsonObject> Object = (*First)->AsObject();
			bool bStable = true;
			TestTrue(TEXT("stable: false"), Object->TryGetBoolField(TEXT("stable"), bStable) && !bStable);
			TestEqual(TEXT("note"), Object->GetStringField(TEXT("note")), FString(TEXT("第一次见面,语气冷淡\n别翻成客套话")));
			TestEqual(TEXT("renamedFrom"), Object->GetStringField(TEXT("renamedFrom")), FString(TEXT("旧对话.1")));
			const TSharedPtr<FJsonObject> Context = Object->GetObjectField(TEXT("context"));
			TestEqual(TEXT("context.speaker"), Context->GetStringField(TEXT("speaker")), FString(TEXT("老周")));
			TestEqual(TEXT("context.args"), Context->GetArrayField(TEXT("args")).Num(), 1);
		}
		const TSharedPtr<FJsonValue>* Steady = Entries.FindByPredicate([](const TSharedPtr<FJsonValue>& Value) { return Value->AsObject()->GetStringField(TEXT("key")) == TEXT("固定"); });
		TestTrue(TEXT("stable is left out when true"), Steady && !(*Steady)->AsObject()->HasField(TEXT("stable")));
	}
	TestEqual(TEXT("the same texts give the same bytes"), WriteDnaLocManifest(Manifest, TEXT("DNarrative/Manifest.dna")), Json);

	// -- where it goes: by the source's path in the project; a file outside the source directory has none
	FString ManifestPath, Source;
	const FString InTree = GetDnaSourceRoot() / TEXT("DnaManifestTest") / TEXT("灯塔.dna");
	if (TestTrue(TEXT("a file in the source directory"), GetDnaLocManifestPath(InTree, ManifestPath, Source)))
	{
		const FString Directory = FPaths::GetBaseFilename(GetDnaSourceRoot());
		TestEqual(TEXT("named from the project"), Source, Directory / TEXT("DnaManifestTest/灯塔.dna"));
		TestTrue(TEXT("under Intermediate"), ManifestPath.EndsWith(TEXT("Intermediate/DLoc/extract/DreamNarrative/") + Source + TEXT(".json")));
		FDnaLocManifest Written = Manifest;
		Written.SourcePath = InTree;
		ON_SCOPE_EXIT { IFileManager::Get().DeleteDirectory(*FPaths::GetPath(ManifestPath), false, true); };
		FString Read;
		TestTrue(TEXT("written"), SaveDnaLocManifest(Written) && FFileHelper::LoadFileToString(Read, *ManifestPath) && Read == WriteDnaLocManifest(Written, Source));
		const FDateTime Stamp = IFileManager::Get().GetTimeStamp(*ManifestPath);
		FPlatformProcess::Sleep(1.1f);
		TestTrue(TEXT("again"), SaveDnaLocManifest(Written));
		TestTrue(TEXT("the same text is not written again"), IFileManager::Get().GetTimeStamp(*ManifestPath) == Stamp);
	}
	TestFalse(TEXT("a file outside it"), GetDnaLocManifestPath(FPaths::ProjectDir() / TEXT("Elsewhere/灯塔.dna"), ManifestPath, Source));

	// -- two texts under one key, an id on a stage entry that is not text
	auto Codes = [](const FString& Text)
	{
		const FString Path = TEXT("Clash.dna");
		const TMap<FString, FString> Texts = { { Path, Text } };
		FDnaProgram Clash;
		FDnaDiagnosticSink Diagnostics;
		LoadDnaProgram({ Path }, Clash, Diagnostics, &Texts);
		if (!Diagnostics.HasErrors())
		{
			FDnaGenerateOptions Transient;
			Transient.bSave = false;
			Transient.bTransient = true;
			FDnaGenerateResult Result;
			GenerateDnaProgram(Clash, Transient, Result, Diagnostics);
		}
		TArray<FString> Out;
		for (const FDnaDiagnostic& Each : Diagnostics.GetDiagnostics()) { if (Each.Severity == EDnaSeverity::Error) { Out.AddUnique(Each.Code); } }
		return Out;
	};
	TestEqual(TEXT("one key, two texts"), Codes(TEXT("character 甲\ndialogue 一 for 甲\n  甲: 一。 #同\ndialogue 二\n  甲: 二。 #同\n")), TArray<FString>{ TEXT("DNS6007") });
	TestTrue(TEXT("only text takes an id"), Codes(TEXT("stage 坏\n  duration 1.0s\n  camera\n    0.0s  shake /Script/Engine.CameraShakeBase #震\n")).Contains(TEXT("DNS7006")));
	return !HasAnyErrors();
}

#endif
