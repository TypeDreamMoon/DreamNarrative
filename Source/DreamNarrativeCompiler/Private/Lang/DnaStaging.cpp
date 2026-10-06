// Copyright DreamMoon. All Rights Reserved.

#include "Lang/DnaStaging.h"
#include "Lang/DnaEditSession.h"
#include "Lang/DnaPrinter.h"
#include "Math/RandomStream.h"

#define LOCTEXT_NAMESPACE "DnaStaging"

namespace UE::DreamNarrative::Lang
{
	namespace
	{
		struct FSpoken
		{
			const FDnaLineNode* Node = nullptr;
			FString Speaker;
			FString Listener;
		};

		void CollectSpeech(const FDnaLineNode& Node, TArray<FSpoken>& Out)
		{
			for (const FDnaLineNodePtr& Child : Node.Children)
			{
				if (!Child.IsValid())
				{
					continue;
				}
				if (Child->Kind == EDnaLineKind::Speech && !Child->Payload.Name.IsEmpty())
				{
					Out.Add({ Child.Get(), Child->Payload.Name, FString() });
				}
				CollectSpeech(*Child, Out);
			}
		}

		/** The lines of one generated stage, each at its level under the file's indentation. */
		TArray<TPair<int32, FString>> MakeStage(const FString& Name, const FString& Speaker, const FString& Listener, FRandomStream& Stream)
		{
			enum class EShot : uint8 { Close, OverShoulder, Medium };
			const EShot Shot = Listener.IsEmpty()
				? (Stream.FRand() < 0.6f ? EShot::Close : EShot::Medium)
				: static_cast<EShot>(Stream.RandRange(0, 2));
			FString Place;
			switch (Shot)
			{
			case EShot::Close:
				Place = FString::Printf(TEXT("local (110, 0, 65) of %s look %s fov 35"), *Speaker, *Speaker);
				break;
			case EShot::OverShoulder:
				// From behind the one listening, past their shoulder, onto the one speaking.
				Place = FString::Printf(TEXT("local (-70, 35, 75) of %s look %s fov 45"), *Listener, *Speaker);
				break;
			case EShot::Medium:
				Place = FString::Printf(TEXT("local (230, 60, 50) of %s look %s fov 50"), *Speaker, *Speaker);
				break;
			}
			TArray<TPair<int32, FString>> Lines;
			Lines.Emplace(0, FString());
			Lines.Emplace(0, TEXT("stage ") + Name);
			Lines.Emplace(1, TEXT("base: line"));
			Lines.Emplace(1, TEXT("shots"));
			Lines.Emplace(2, TEXT("镜头     ") + Place);
			Lines.Emplace(1, TEXT("camera"));
			Lines.Emplace(2, TEXT("0%       cut -> 镜头 hard"));
			if (Stream.FRand() < 0.5f)
			{
				Lines.Emplace(2, FString::Printf(TEXT("0%%       push %dcm over 100%%"), Stream.RandRange(4, 10)));
			}
			Lines.Emplace(1, TEXT("restore"));
			Lines.Emplace(2, TEXT("camera   blend back 0%"));
			return Lines;
		}
	}

	FString GetDnaStagingPrefix(const FString& Dialogue)
	{
		return TEXT("演出_") + Dialogue + TEXT("_");
	}

	bool StageDnaDialogue(FDnaEditSession& Session, const FString& Dialogue, int32 Seed, FText& OutError)
	{
		const FDnaLineNode* Root = Session.FindDeclaration(EDnaLineKind::Dialogue, Dialogue);
		if (!Root)
		{
			OutError = FText::Format(LOCTEXT("NoDialogue", "There is no dialogue called {0} in this file"), FText::FromString(Dialogue));
			return false;
		}
		const FString Prefix = GetDnaStagingPrefix(Dialogue);
		TArray<FSpoken> Spoken;
		CollectSpeech(*Root, Spoken);
		Spoken.RemoveAll([&Prefix](const FSpoken& Each)
		{
			const FString& Own = Each.Node->Payload.Modifiers.Stage;
			return !Own.IsEmpty() && !Own.StartsWith(Prefix);
		});
		if (Spoken.Num() == 0)
		{
			OutError = FText::Format(LOCTEXT("NobodySpeaks", "Nobody speaks in {0} (or every line has a stage of its own)"), FText::FromString(Dialogue));
			return false;
		}
		// Who each line is said to: the last one who spoke before, else the next one who will.
		for (int32 Index = 0; Index < Spoken.Num(); ++Index)
		{
			for (int32 Back = Index - 1; Back >= 0 && Spoken[Index].Listener.IsEmpty(); --Back)
			{
				if (Spoken[Back].Speaker != Spoken[Index].Speaker) { Spoken[Index].Listener = Spoken[Back].Speaker; }
			}
			for (int32 Ahead = Index + 1; Ahead < Spoken.Num() && Spoken[Index].Listener.IsEmpty(); ++Ahead)
			{
				if (Spoken[Ahead].Speaker != Spoken[Index].Speaker) { Spoken[Index].Listener = Spoken[Ahead].Speaker; }
			}
		}

		// One stage per speaker and listener, in the order they first speak.
		TArray<TPair<int32, FString>> Added;
		TMap<FString, FString> StageOf;
		for (const FSpoken& Each : Spoken)
		{
			const FString Key = Each.Speaker + TEXT("|") + Each.Listener;
			if (StageOf.Contains(Key))
			{
				continue;
			}
			const FString Name = Prefix + Each.Speaker + (Each.Listener.IsEmpty() ? FString() : TEXT("_对") + Each.Listener);
			StageOf.Add(Key, Name);
			FRandomStream Stream(Seed * 7919 + static_cast<int32>(GetTypeHash(Key) & 0x7FFFFF));
			Added.Append(MakeStage(Name, Each.Speaker, Each.Listener, Stream));
		}

		// What an earlier run made goes: its stages, and the blank line before each.
		const TArray<FDnaRawLine>& Lines = Session.GetDocument().Scan.Lines;
		TArray<TPair<int32, int32>> Removed;
		for (const FDnaLineNodePtr& Other : Session.GetDocument().Roots)
		{
			if (Other.IsValid() && Other->Kind == EDnaLineKind::Stage && Other->Payload.Name.StartsWith(Prefix))
			{
				int32 First = 0;
				int32 Last = 0;
				GetDnaNodeLineRange(*Other, First, Last);
				if (First > 1 && Lines[First - 2].IsBlank())
				{
					--First;
				}
				Removed.Emplace(First, Last);
			}
		}
		int32 DialogueFirst = 0;
		int32 DialogueLast = 0;
		GetDnaNodeLineRange(*Root, DialogueFirst, DialogueLast);

		return Session.ApplyEdit([&](FDnaSourceEdit& Printer, FText&)
		{
			for (const FSpoken& Each : Spoken)
			{
				const FString& Name = StageOf[Each.Speaker + TEXT("|") + Each.Listener];
				const FString& Old = Each.Node->Payload.Modifiers.Stage;
				FString Content = Each.Node->Raw.Content;
				if (!Old.IsEmpty())
				{
					Content.ReplaceInline(*(TEXT("@") + Old), *(TEXT("@") + Name), ESearchCase::CaseSensitive);
				}
				else
				{
					Content = Content.TrimEnd() + TEXT(" @") + Name;
				}
				Printer.SetContent(Each.Node->LineNumber(), Content);
			}
			for (const TPair<int32, int32>& Range : Removed)
			{
				Printer.Remove(Range.Key, Range.Value);
			}
			return Printer.InsertAfter(DialogueLast, Added);
		}, OutError);
	}
}

#undef LOCTEXT_NAMESPACE
