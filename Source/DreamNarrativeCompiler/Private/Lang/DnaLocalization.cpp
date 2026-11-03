// Copyright DreamMoon. All Rights Reserved.

#include "Lang/DnaLocalization.h"
#include "HAL/FileManager.h"
#include "Lang/DnaProject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace UE::DreamNarrative::Lang
{
	// ---------------------------------------------------------------- extraction manifests

	namespace
	{
		FString JsonString(const FString& Text)
		{
			FString Out;
			Out.Reserve(Text.Len() + 2);
			Out.AppendChar(TEXT('"'));
			for (const TCHAR Char : Text)
			{
				switch (Char)
				{
				case TEXT('"'): Out += TEXT("\\\""); break;
				case TEXT('\\'): Out += TEXT("\\\\"); break;
				case TEXT('\n'): Out += TEXT("\\n"); break;
				case TEXT('\r'): Out += TEXT("\\r"); break;
				case TEXT('\t'): Out += TEXT("\\t"); break;
				default:
					if (Char < 0x20) { Out += FString::Printf(TEXT("\\u%04x"), static_cast<uint32>(Char)); }
					else { Out.AppendChar(Char); }
					break;
				}
			}
			Out.AppendChar(TEXT('"'));
			return Out;
		}
	}

	FString WriteDnaLocManifest(const FDnaLocManifest& Manifest, const FString& Source)
	{
		FString Json = TEXT("{\n");
		Json += TEXT("  \"version\": 1,\n");
		Json += TEXT("  \"provider\": \"DreamNarrative\",\n");
		Json += FString::Printf(TEXT("  \"source\": %s,\n"), *JsonString(Source));
		Json += FString::Printf(TEXT("  \"namespace\": %s,\n"), *JsonString(Manifest.Namespace));
		if (Manifest.Entries.Num() == 0)
		{
			return Json + TEXT("  \"entries\": []\n}\n");
		}
		// One entry a line, its members in a fixed order: the same texts give the same bytes.
		Json += TEXT("  \"entries\": [\n");
		for (int32 Index = 0; Index < Manifest.Entries.Num(); ++Index)
		{
			const FDnaLocEntry& Entry = Manifest.Entries[Index];
			TArray<FString> Members;
			Members.Add(TEXT("\"key\": ") + JsonString(Entry.Key));
			Members.Add(TEXT("\"src\": ") + JsonString(Entry.Source));
			Members.Add(TEXT("\"at\": ") + JsonString(Entry.At));
			Members.Add(TEXT("\"kind\": ") + JsonString(Entry.Kind));
			if (!Entry.Note.IsEmpty()) { Members.Add(TEXT("\"note\": ") + JsonString(Entry.Note)); }
			if (!Entry.bStable) { Members.Add(TEXT("\"stable\": false")); }
			if (!Entry.RenamedFrom.IsEmpty()) { Members.Add(TEXT("\"renamedFrom\": ") + JsonString(Entry.RenamedFrom)); }
			if (Entry.Context.Num() > 0 || Entry.Args.Num() > 0)
			{
				TArray<FString> Context;
				for (const TPair<FString, FString>& Pair : Entry.Context) { Context.Add(JsonString(Pair.Key) + TEXT(": ") + JsonString(Pair.Value)); }
				if (Entry.Args.Num() > 0)
				{
					Context.Add(TEXT("\"args\": [") + FString::JoinBy(Entry.Args, TEXT(", "), [](const FString& Arg) { return JsonString(Arg); }) + TEXT("]"));
				}
				Members.Add(TEXT("\"context\": { ") + FString::Join(Context, TEXT(", ")) + TEXT(" }"));
			}
			Json += TEXT("    { ") + FString::Join(Members, TEXT(", ")) + (Index + 1 < Manifest.Entries.Num() ? TEXT(" },\n") : TEXT(" }\n"));
		}
		return Json + TEXT("  ]\n}\n");
	}

	bool GetDnaLocManifestPath(const FString& SourcePath, FString& OutManifestPath, FString& OutSource)
	{
		FString File = FPaths::ConvertRelativePathToFull(SourcePath);
		FPaths::NormalizeFilename(File);
		FString Root = GetDnaSourceRoot();
		FPaths::NormalizeDirectoryName(Root);
		FString Project = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
		FPaths::NormalizeDirectoryName(Project);
		if (!File.StartsWith(Root + TEXT("/"), ESearchCase::IgnoreCase) || !File.StartsWith(Project + TEXT("/"), ESearchCase::IgnoreCase))
		{
			return false;
		}
		OutSource = File.RightChop(Project.Len() + 1);
		OutManifestPath = Project / TEXT("Intermediate/DLoc/extract/DreamNarrative") / (OutSource + TEXT(".json"));
		return true;
	}

	bool SaveDnaLocManifest(const FDnaLocManifest& Manifest)
	{
		FString Path, Source;
		if (!GetDnaLocManifestPath(Manifest.SourcePath, Path, Source))
		{
			return true;
		}
		const FString Json = WriteDnaLocManifest(Manifest, Source);
		FString Existing;
		if (FFileHelper::LoadFileToString(Existing, *Path) && Existing == Json)
		{
			return true;
		}
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), /*Tree=*/true);
		return FFileHelper::SaveStringToFile(Json, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}
}
