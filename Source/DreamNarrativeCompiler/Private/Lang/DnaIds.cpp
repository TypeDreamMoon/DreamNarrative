// Copyright DreamMoon. All Rights Reserved.

#include "Lang/DnaIds.h"
#include "Lang/DnaEditSession.h"
#include "Lang/DnaPrinter.h"

#define LOCTEXT_NAMESPACE "DnaIds"

namespace UE::DreamNarrative::Lang
{
	void CollectDnaGeneratedIds(const TArray<FDnaLocManifest>& Manifests, TMap<FString, TMap<int32, FString>>& OutBySource)
	{
		for (const FDnaLocManifest& Manifest : Manifests)
		{
			TMap<int32, FString>& Ids = OutBySource.FindOrAdd(Manifest.SourcePath);
			for (const FDnaLocEntry& Entry : Manifest.Entries)
			{
				if (!Entry.FreezeId.IsEmpty() && Entry.SourceLine > 0) { Ids.Add(Entry.SourceLine, Entry.FreezeId); }
			}
		}
	}

	bool FreezeDnaIds(FDnaEditSession& Session, const TMap<int32, FString>& Ids, int32& OutFrozen, FText& OutError)
	{
		OutFrozen = 0;
		if (Ids.Num() == 0)
		{
			return true;
		}
		int32 Frozen = 0;
		const bool bOk = Session.ApplyEdit([&Ids, &Frozen](FDnaSourceEdit& Printer, FText& Error)
		{
			for (const TPair<int32, FString>& Pair : Ids)
			{
				// The id goes last: after the text, after whatever modifiers the line already has.
				const FString Content = Printer.GetContent(Pair.Key).TrimEnd();
				if (!Printer.SetContent(Pair.Key, Content + TEXT(" #") + Pair.Value))
				{
					Error = FText::Format(LOCTEXT("NoLine", "There is no line {0}"), FText::AsNumber(Pair.Key));
					return false;
				}
				++Frozen;
			}
			return true;
		}, OutError);
		if (!bOk)
		{
			return false;
		}
		// Every id written has to read back as the line's own: otherwise the text swallowed it.
		TMap<int32, FString> ReadBack;
		Session.GetDocument().ForEachNode([&ReadBack](const FDnaLineNode& Node) { ReadBack.Add(Node.LineNumber(), Node.Payload.Modifiers.Id); });
		for (const TPair<int32, FString>& Pair : Ids)
		{
			const FString* Read = ReadBack.Find(Pair.Key);
			if (!Read || *Read != Pair.Value)
			{
				Session.Undo();
				OutError = FText::Format(LOCTEXT("IdNotRead", "Line {0} does not read `#{1}` back as its id; nothing was frozen"), FText::AsNumber(Pair.Key), FText::FromString(Pair.Value));
				return false;
			}
		}
		OutFrozen = Frozen;
		return true;
	}
}

#undef LOCTEXT_NAMESPACE
