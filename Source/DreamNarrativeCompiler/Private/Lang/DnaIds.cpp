// Copyright DreamMoon. All Rights Reserved.

#include "Lang/DnaIds.h"
#include "DreamNarrativeDialogue.h"
#include "DreamNarrativeScript.h"
#include "Lang/DnaEditSession.h"
#include "Lang/DnaPrinter.h"

#define LOCTEXT_NAMESPACE "DnaIds"

namespace UE::DreamNarrative::Lang
{
	void CollectDnaGeneratedIds(const TArray<UDreamNarrativeScript*>& Scripts, TMap<FString, TMap<int32, FString>>& OutBySource)
	{
		for (const UDreamNarrativeScript* Script : Scripts)
		{
			const UDnaDialogueSection* Section = Script ? Script->FindSection<UDnaDialogueSection>() : nullptr;
			if (!Section)
			{
				continue;
			}
			TMap<int32, FString>& Ids = OutBySource.FindOrAdd(Script->SourcePath);
			for (const FDnaDialogueNodeData& Node : Section->Nodes)
			{
				for (const FDnaLineData& Line : Node.Lines)
				{
					if (!Line.bExplicitId && Line.SourceLine > 0) { Ids.Add(Line.SourceLine, Line.Id.ToString()); }
				}
				for (const FDnaChoiceGroupData& Group : Node.ChoiceGroups)
				{
					for (const FDnaChoiceData& Option : Group.Options)
					{
						if (!Option.bExplicitId && Option.SourceLine > 0) { Ids.Add(Option.SourceLine, Option.Id.ToString()); }
					}
				}
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
