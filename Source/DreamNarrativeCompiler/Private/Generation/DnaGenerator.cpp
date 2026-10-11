// Copyright DreamMoon. All Rights Reserved.

#include "Generation/DnaGenerator.h"
#include "DreamNarrativeCompilerModule.h"
#include "DreamNarrativeArchive.h"
#include "DreamNarrativeDialogue.h"
#include "DreamNarrativeQuest.h"
#include "DreamNarrativeScript.h"
#include "DreamNarrativeSettings.h"
#include "DreamNarrativeStageData.h"
#include "Generation/DnaExprCompiler.h"
#include "Lang/DnaLexer.h"
#include "Lang/DnaProject.h"
#include "Lang/DnaStageCompiler.h"
#include "Lang/DnaVoice.h"
#include "Misc/FileHelper.h"
#include "Sound/SoundBase.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/IAssetRegistry.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#endif

#define LOCTEXT_NAMESPACE "DreamNarrativeLang"

namespace UE::DreamNarrative::Lang
{
	FString GetDnaPackageName(const FString& SourcePath, const FString& AssetDirectory)
	{
		FString Directory = AssetDirectory.IsEmpty() ? UDreamNarrativeSettings::Get()->AssetDirectory : AssetDirectory;
		Directory.RemoveFromEnd(TEXT("/"));
		return Directory / FPaths::GetBaseFilename(SourcePath);
	}

	namespace
	{
		/** The class of a stage verb's asset, as the asset registry names it; none for a verb without one. */
		FTopLevelAssetPath StageAssetClass(EDnaStageVerb Verb)
		{
			switch (Verb)
			{
			case EDnaStageVerb::Montage: return FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("AnimMontage"));
			case EDnaStageVerb::Spawn: return FTopLevelAssetPath(TEXT("/Script/Niagara"), TEXT("NiagaraSystem"));
			case EDnaStageVerb::PlaySequence: return FTopLevelAssetPath(TEXT("/Script/LevelSequence"), TEXT("LevelSequence"));
			case EDnaStageVerb::Sound:
			case EDnaStageVerb::Music: return FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("SoundBase"));
			case EDnaStageVerb::Shake: return FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("Blueprint"));
			default: return FTopLevelAssetPath();
			}
		}

		/**
		 * Before a search: a commandlet (a build, a cook) discovers no assets by itself, and an editor may
		 * still be discovering them in the background. Directory is scanned now, once.
		 */
		void ScanForSearch(IAssetRegistry& Registry, const FString& Directory)
		{
			if (!IsRunningCommandlet() && !Registry.IsLoadingAssets())
			{
				return;
			}
			static TSet<FString> Scanned;
			bool bAlready = false;
			Scanned.Add(Directory, &bAlready);
			if (!bAlready)
			{
				Registry.ScanPathsSynchronous({ Directory }, /*bForceRescan=*/false);
			}
		}

		/** The assets answering to NameOrPath, of Class or a class under it: a content path directly, a bare name anywhere under Roots. */
		TArray<FAssetData> FindDnaAssets(const FString& NameOrPath, const FTopLevelAssetPath& Class, const TArray<FString>& Roots)
		{
			TArray<FAssetData> Found;
			IAssetRegistry* Registry = IAssetRegistry::Get();
			if (!Registry || NameOrPath.IsEmpty() || !Class.IsValid())
			{
				return Found;
			}
			const UClass* ClassObject = FindObject<UClass>(Class);
			auto IsWanted = [ClassObject, &Class](const FAssetData& Asset)
			{
				return ClassObject ? Asset.IsInstanceOf(ClassObject) : Asset.AssetClassPath == Class;
			};
			if (NameOrPath.StartsWith(TEXT("/")))
			{
				FString Path = NameOrPath;
				if (!Path.Contains(TEXT(".")))
				{
					Path += TEXT(".") + FPackageName::GetShortName(Path);
				}
				ScanForSearch(*Registry, FPackageName::GetLongPackagePath(Path));
				const FAssetData Asset = Registry->GetAssetByObjectPath(FSoftObjectPath(Path));
				if (Asset.IsValid() && IsWanted(Asset)) { Found.Add(Asset); }
				return Found;
			}
			const FName Wanted(*NameOrPath);
			for (const FString& Root : Roots)
			{
				ScanForSearch(*Registry, Root);
				FARFilter Filter;
				Filter.PackagePaths.Add(FName(*Root));
				Filter.bRecursivePaths = true;
				Filter.ClassPaths.Add(Class);
				Filter.bRecursiveClasses = true;
				TArray<FAssetData> Assets;
				Registry->GetAssets(Filter, Assets);
				for (const FAssetData& Asset : Assets)
				{
					if (Asset.AssetName == Wanted) { Found.AddUnique(Asset); }
				}
			}
			return Found;
		}

		/**
		 * Finds a stage entry's asset now, so the script names it by path: a cook takes it along, and a
		 * playing stage loads it without searching. A bare name is looked for under the stage asset roots;
		 * one not found stays as written and is looked for again when the stage plays.
		 */
		void ResolveStageAsset(FDnaStageEntry& Entry, const FString& Stage, bool bReport, TArray<FSoftObjectPath>& OutReferences, FDnaDiagnosticSink& Diagnostics)
		{
			const FTopLevelAssetPath Class = StageAssetClass(Entry.Verb);
			// A native class (`/Script/...`) is no asset to find or cook.
			if (!Class.IsValid() || Entry.Asset.IsEmpty() || Entry.Asset.StartsWith(TEXT("/Script/")))
			{
				return;
			}
			const TArray<FAssetData> Found = FindDnaAssets(Entry.Asset, Class, UDreamNarrativeSettings::Get()->StageAssetRoots);
			FDnaSpan Span;
			Span.Line = FMath::Max(1, Entry.SourceLine);
			if (Found.Num() == 0)
			{
				if (bReport)
				{
					Diagnostics.Warning(TEXT("DNS6001"), Span, FText::Format(LOCTEXT("StageAssetMissing",
						"Stage {0} plays '{1}', which no asset of type {2} answers to under the stage asset roots; a cook will not take it along"),
						FText::FromString(Stage), FText::FromString(Entry.Asset), FText::FromString(Class.GetAssetName().ToString())));
				}
				return;
			}
			if (Found.Num() > 1 && bReport)
			{
				Diagnostics.Warning(TEXT("DNS6002"), Span, FText::Format(LOCTEXT("StageAssetAmbiguous",
					"Stage {0} plays '{1}', a name {2} assets answer to; it plays {3}. Write the path to choose"),
					FText::FromString(Stage), FText::FromString(Entry.Asset), FText::AsNumber(Found.Num()), FText::FromString(Found[0].GetObjectPathString())));
			}
			Entry.Asset = Found[0].GetObjectPathString();
			OutReferences.AddUnique(Found[0].GetSoftObjectPath());
		}

		/** The voice tables of one generation, and which line ids the program has. */
		struct FVoiceContext
		{
			TMap<FString, FDnaVoiceEntry> Entries;
			TSet<FString> LineIds;
		};

		/**
		 * Finds the voice of every line of a dialogue section and writes it, with its length, into the
		 * line: the line's own `voice`, else the speaker's lookup -- the convention under its root, or
		 * the voice tables -- else the project's default. A line whose voice is looked for and not
		 * found is DNS6003.
		 */
		void ResolveVoices(UDnaDialogueSection& Section, const TMap<FName, FString>& Overrides, const FDnaProgram& Program, FVoiceContext& Voices,
			TArray<FSoftObjectPath>& OutReferences, FDnaDiagnosticSink& Diagnostics)
		{
			const UDreamNarrativeSettings* Settings = UDreamNarrativeSettings::Get();
			const FTopLevelAssetPath SoundClass(TEXT("/Script/Engine"), TEXT("SoundBase"));
			for (FDnaDialogueNodeData& Node : Section.Nodes)
			{
				for (FDnaLineData& Line : Node.Lines)
				{
					const FString Id = Line.Id.ToString();
					Voices.LineIds.Add(Id);
					FString Root;
					const EDnaVoiceLookup Lookup = GetDnaVoiceLookup(Program, Line.Speaker, Root);
					FString Wanted;
					FText Looked;
					if (const FString* Own = Overrides.Find(Line.Id))
					{
						Wanted = *Own;
						Looked = FText::Format(LOCTEXT("VoiceOwn", "its own voice '{0}'"), FText::FromString(Wanted));
					}
					else if (Lookup == EDnaVoiceLookup::Convention)
					{
						Wanted = Root / GetDnaVoiceAssetName(Id);
						Looked = FText::Format(LOCTEXT("VoiceConvention", "'{0}'"), FText::FromString(Wanted));
					}
					else if (Lookup == EDnaVoiceLookup::Table)
					{
						if (const FDnaVoiceEntry* Entry = Voices.Entries.Find(Id))
						{
							Wanted = Entry->Voice;
							Looked = FText::Format(LOCTEXT("VoiceTable", "'{0}' (from {1})"), FText::FromString(Wanted), FText::FromString(Entry->Source));
						}
						else
						{
							Looked = LOCTEXT("VoiceNoRow", "a row in the voice tables");
						}
					}
					else
					{
						continue;
					}
					const TArray<FAssetData> Found = Wanted.IsEmpty() ? TArray<FAssetData>() : FindDnaAssets(Wanted, SoundClass, { Root, Settings->DefaultVoiceRoot });
					if (Found.Num() == 0)
					{
						FDnaSpan Span;
						Span.Line = FMath::Max(1, Line.SourceLine);
						const FText Message = FText::Format(LOCTEXT("VoiceMissing", "Line '{0}' has no voice: looked for {1}"), FText::FromString(Id), Looked);
						if (Settings->bMissingVoiceIsError) { Diagnostics.Error(TEXT("DNS6003"), Span, Message); }
						else { Diagnostics.Warning(TEXT("DNS6003"), Span, Message); }
						continue;
					}
					Line.Voice = TSoftObjectPtr<USoundBase>(Found[0].GetSoftObjectPath());
					float Seconds = 0.f;
					Found[0].GetTagValue(TEXT("Duration"), Seconds);
					Line.VoiceSeconds = FMath::Max(0.f, Seconds);
					OutReferences.AddUnique(Found[0].GetSoftObjectPath());
				}
			}
		}

		/** A run of text parts becomes one FText pattern plus the expressions its placeholders take. */
		struct FTextBuilder
		{
			FString Pattern;
			TArray<int32> Args;

			void Append(const FDnaText& Text, FDnaExprCompiler& Compiler)
			{
				for (const FDnaTextPart& Part : Text.Parts)
				{
					if (Part.IsExpr())
					{
						Pattern += FString::Printf(TEXT("{%d}"), Args.Num());
						Args.Add(Compiler.Compile(Part.Expr));
					}
					else
					{
						// Braces are format syntax; a backtick escapes them.
						FString Literal = Part.Literal;
						Literal.ReplaceInline(TEXT("`"), TEXT("``"));
						Literal.ReplaceInline(TEXT("{"), TEXT("`{"));
						Literal.ReplaceInline(TEXT("}"), TEXT("`}"));
						Pattern += Literal;
					}
				}
			}
		};

		FText MakeLocalizable(const FString& Namespace, const FString& Key, const FString& Source)
		{
#if WITH_EDITOR
			return FText::AsLocalizable_Advanced(FTextKey(Namespace), FTextKey(Key), *Source);
#else
			return FText::FromString(Source);
#endif
		}

		/** One command line (`set`, `accept`, `play`, ...) as an instruction at the end of Code. */
		void AppendCommand(TArray<FDnaInstruction>& Code, const FDnaLineNode& Line, FDnaExprCompiler& Exprs, const FDnaProgram& Program)
		{
			const FDnaLinePayload& P = Line.Payload;
			FDnaInstruction& Ins = Code.AddDefaulted_GetRef();
			Ins.SourceLine = Line.LineNumber();
			Ins.Name = FName(*P.Name);
			switch (P.Command)
			{
			case EDnaCommandKind::Set:
				Ins.Op = EDnaOpCode::Set;
				Ins.Expr = Exprs.CompileValueFor(P.Expr, Program.FindFact(P.Name));
				Ins.Assign = P.AssignOp == EDnaTokenKind::PlusAssign ? EDnaAssignOp::Add
					: P.AssignOp == EDnaTokenKind::MinusAssign ? EDnaAssignOp::Sub : EDnaAssignOp::Set;
				break;
			case EDnaCommandKind::Accept: Ins.Op = EDnaOpCode::Accept; break;
			case EDnaCommandKind::Complete: Ins.Op = EDnaOpCode::Complete; break;
			case EDnaCommandKind::Fail: Ins.Op = EDnaOpCode::Fail; break;
			case EDnaCommandKind::Advance: Ins.Op = EDnaOpCode::Advance; break;
			case EDnaCommandKind::Emit:
			{
				Ins.Op = EDnaOpCode::Emit;
				const FDnaEventInfo* Event = Program.FindEvent(P.Name);
				for (const FDnaEmitArg& Arg : P.EmitArgs)
				{
					// A bare member of a tag parameter's set means that member.
					Ins.ArgNames.Add(FName(*Arg.Name));
					Ins.ArgExprs.Add(Exprs.CompileValueFor(Arg.Value, Event ? Event->FindParam(Arg.Name) : nullptr));
				}
				break;
			}
			case EDnaCommandKind::Play:
				Ins.Op = EDnaOpCode::Play;
				Ins.bAsync = P.bAsync;
				break;
			case EDnaCommandKind::Wait:
				Ins.Op = EDnaOpCode::Wait;
				Ins.Name = NAME_None;
				Ins.Seconds = static_cast<float>(P.Seconds);
				break;
			default:
				Code.Pop();
				break;
			}
		}

		FDnaQuestText MakeQuestText(const FDnaText& Text, FDnaExprCompiler& Exprs, const FString& Namespace, const FString& Key)
		{
			FTextBuilder Builder;
			Builder.Append(Text, Exprs);
			FDnaQuestText Out;
			Out.Text = MakeLocalizable(Namespace, Key, Builder.Pattern);
			Out.Args = MoveTemp(Builder.Args);
			return Out;
		}

		/** An archive's title and entries, from its block. */
		void CompileArchive(const FDnaLineNode& Root, FDnaArchiveData& Out, FDnaExprCompiler& Exprs, const FString& Namespace)
		{
			const FDnaLinePayload& P = Root.Payload;
			const FString Key = TEXT("archive.") + P.Name;
			Out.Name = FName(*P.Name);
			Out.Title = FText::FromString(P.Name);
			Out.bProfile = P.bProfile;
			Out.SourceLine = Root.LineNumber();
			TArray<FDnaLineNode*> Lines;
			Root.GetContentChildren(Lines);
			for (const FDnaLineNode* Line : Lines)
			{
				if (Line->Kind == EDnaLineKind::Property && Line->Payload.Name == TEXT("title"))
				{
					Out.Title = MakeLocalizable(Namespace, Key + TEXT(".title"), Line->Payload.Value);
					continue;
				}
				if (Line->Kind != EDnaLineKind::ArchiveEntry)
				{
					continue;
				}
				FDnaArchiveEntryData& Entry = Out.Entries.AddDefaulted_GetRef();
				Entry.Condition = Exprs.Compile(Line->Payload.Condition);
				Entry.SourceLine = Line->LineNumber();
				// The lines under it, one after another.
				FTextBuilder Builder;
				TFunction<void(const FDnaLineNode&)> Append = [&Builder, &Exprs, &Append](const FDnaLineNode& Node)
				{
					for (const FDnaLineNodePtr& Child : Node.Children)
					{
						if (Child.IsValid() && Child->Kind == EDnaLineKind::Continuation)
						{
							if (!Builder.Pattern.IsEmpty()) { Builder.Pattern += TEXT("\n"); }
							Builder.Append(Child->Payload.Text, Exprs);
							Append(*Child);
						}
					}
				};
				Append(*Line);
				Entry.Text.Text = MakeLocalizable(Namespace, FString::Printf(TEXT("%s.entry%d"), *Key, Out.Entries.Num() - 1), Builder.Pattern);
				Entry.Text.Args = MoveTemp(Builder.Args);
			}
		}

		/** A quest's rules, from its block. Validation already checked every name and value. */
		void CompileQuest(const FDnaLineNode& Root, FDnaQuestData& Out, FDnaExprCompiler& Exprs, const FDnaProgram& Program, const FString& Namespace)
		{
			const FDnaLinePayload& P = Root.Payload;
			const FString Key = TEXT("quest.") + P.Name;
			Out.Name = FName(*P.Name);
			Out.Title = FText::FromString(P.Name);
			Out.SourceLine = Root.LineNumber();
			Out.WasName = P.WasName.IsEmpty() ? NAME_None : FName(*P.WasName);

			TArray<FDnaLineNode*> Lines;
			Root.GetContentChildren(Lines);
			for (const FDnaLineNode* Line : Lines)
			{
				const FDnaLinePayload& L = Line->Payload;
				TArray<FDnaLineNode*> Children;
				Line->GetContentChildren(Children);
				switch (Line->Kind)
				{
				case EDnaLineKind::Property:
					if (L.Name == TEXT("title"))
					{
						Out.Title = MakeLocalizable(Namespace, Key + TEXT(".title"), L.Value);
					}
					else if (L.Name == TEXT("priority"))
					{
						Out.Priority = L.Value == TEXT("main") ? EDnaQuestPriority::Main : L.Value == TEXT("hidden") ? EDnaQuestPriority::Hidden : EDnaQuestPriority::Side;
					}
					else if (L.Name == TEXT("timeout"))
					{
						double Seconds = 0.0;
						Out.Timeout = TryParseDnaDuration(L.Value, Seconds) ? static_cast<float>(Seconds) : 0.f;
					}
					else if (L.Name == TEXT("gameplay"))
					{
						Out.GameplayTag = FName(*L.Value.TrimStartAndEnd());
					}
					break;
				case EDnaLineKind::Flag:
					Out.bTrack |= L.Name == TEXT("track");
					break;
				case EDnaLineKind::Available:
					Out.Available = Exprs.Compile(L.Condition);
					break;
				case EDnaLineKind::FailIf:
					Out.FailIf = Exprs.Compile(L.Condition);
					break;
				case EDnaLineKind::Journal:
					for (const FDnaLineNode* Entry : Children)
					{
						if (Entry->Kind != EDnaLineKind::Property) { continue; }
						const FString& Which = Entry->Payload.Name;
						FDnaQuestText* Slot = Which == TEXT("accepted") ? &Out.JournalAccepted : Which == TEXT("going") ? &Out.JournalGoing
							: Which == TEXT("completed") ? &Out.JournalCompleted : Which == TEXT("failed") ? &Out.JournalFailed : nullptr;
						if (Slot)
						{
							*Slot = MakeQuestText(Entry->Payload.Text, Exprs, Namespace, Key + TEXT(".journal.") + Which);
						}
					}
					break;
				case EDnaLineKind::Objective:
				{
					FDnaObjectiveData& Objective = Out.Objectives.AddDefaulted_GetRef();
					Objective.Condition = Exprs.Compile(L.Condition);
					Objective.bOptional = L.bOptional;
					Objective.SourceLine = Line->LineNumber();
					for (const FDnaLineNode* Entry : Children)
					{
						if (Entry->Kind == EDnaLineKind::Property && Entry->Payload.Name == TEXT("show"))
						{
							Objective.Show = MakeQuestText(Entry->Payload.Text, Exprs, Namespace, FString::Printf(TEXT("%s.objective%d"), *Key, Out.Objectives.Num()));
						}
					}
					break;
				}
				case EDnaLineKind::On:
				{
					FDnaQuestHandlerData& Handler = L.OnKind == EDnaOnKind::Accept ? Out.OnAccept : L.OnKind == EDnaOnKind::Complete ? Out.OnComplete
						: L.OnKind == EDnaOnKind::Fail ? Out.OnFail : Out.OnTimeout;
					for (const FDnaLineNode* Entry : Children)
					{
						if (Entry->Kind == EDnaLineKind::Reward)
						{
							FDnaReward& Reward = Handler.Rewards.AddDefaulted_GetRef();
							Reward.Category = FName(*Entry->Payload.RewardCategory);
							Reward.Item = Entry->Payload.RewardItem.IsEmpty() ? NAME_None : FName(*Entry->Payload.RewardItem);
							Reward.Amount = Entry->Payload.RewardAmount;
						}
						else if (Entry->Kind == EDnaLineKind::Command)
						{
							AppendCommand(Handler.Code, *Entry, Exprs, Program);
						}
					}
					break;
				}
				case EDnaLineKind::Subquest:
				{
					FDnaSubquestData& Subquest = Out.Subquests.AddDefaulted_GetRef();
					Subquest.Quest = FName(*L.Name);
					Subquest.bAuto = L.bAuto;
					break;
				}
				default:
					break;
				}
			}
		}

		class FDialogueCompiler
		{
		public:
			FDialogueCompiler(FDnaExprCompiler& InExprs, const FDnaProgram& InProgram, const FString& InNamespace)
				: Exprs(InExprs), Program(InProgram), Namespace(InNamespace)
			{
			}

			void CompileNode(const FDnaLineNode& Root, FDnaDialogueNodeData& Out)
			{
				const FDnaLinePayload& P = Root.Payload;
				Data = &Out;
				Out.Name = FName(*P.Name);
				Out.ForCharacter = P.ForCharacter.IsEmpty() ? NAME_None : FName(*P.ForCharacter);
				Out.OnEvent = P.OnEvent.IsEmpty() ? NAME_None : FName(*P.OnEvent);
				Out.Channel = P.Channel.IsEmpty() || P.Channel == TEXT("main") ? NAME_None : FName(*P.Channel);
				Out.bOnce = P.bOnceEntry;
				Out.Cooldown = static_cast<float>(P.CooldownSeconds);
				Out.bResume = P.bResume;
				Out.Condition = Exprs.Compile(P.Condition);
				Out.Priority = P.Priority != INDEX_NONE ? P.Priority : FDnaExprCompiler::CountConjuncts(P.Condition);
				Out.bExplicitPriority = P.Priority != INDEX_NONE;
				Out.SourceLine = Root.LineNumber();
				Out.WasName = P.WasName.IsEmpty() ? NAME_None : FName(*P.WasName);
				LineOrdinal = 0;
				ChoiceOrdinal = 0;

				TArray<FDnaLineNode*> Lines;
				Root.GetContentChildren(Lines);
				CompileBlock(Lines);
				Emit(EDnaOpCode::End, Root.LineNumber());
			}

			/** Lines that named their own voice (`voice 资源`), by id. */
			TMap<FName, FString> VoiceOverrides;

		private:
			FDnaExprCompiler& Exprs;
			const FDnaProgram& Program;
			FString Namespace;
			FDnaDialogueNodeData* Data = nullptr;
			int32 LineOrdinal = 0;
			int32 ChoiceOrdinal = 0;

			int32 Emit(EDnaOpCode Op, int32 SourceLine)
			{
				FDnaInstruction& Ins = Data->Code.AddDefaulted_GetRef();
				Ins.Op = Op;
				Ins.SourceLine = SourceLine;
				return Data->Code.Num() - 1;
			}

			FDnaInstruction& At(int32 Index) { return Data->Code[Index]; }

			void CompileBlock(const TArray<FDnaLineNode*>& Lines)
			{
				int32 Index = 0;
				while (Index < Lines.Num())
				{
					const FDnaLineNode& Line = *Lines[Index];
					switch (Line.Kind)
					{
					case EDnaLineKind::Speech:
					case EDnaLineKind::Narration:
						CompileTextLine(Line);
						++Index;
						break;
					case EDnaLineKind::Choice:
					{
						int32 End = Index;
						while (End < Lines.Num() && Lines[End]->Kind == EDnaLineKind::Choice) { ++End; }
						CompileChoiceGroup(Lines, Index, End);
						Index = End;
						break;
					}
					case EDnaLineKind::If:
					{
						int32 End = Index + 1;
						while (End < Lines.Num() && (Lines[End]->Kind == EDnaLineKind::ElseIf || Lines[End]->Kind == EDnaLineKind::Else)) { ++End; }
						CompileIfChain(Lines, Index, End);
						Index = End;
						break;
					}
					case EDnaLineKind::Jump:
						if (Line.Payload.bEnd)
						{
							Emit(EDnaOpCode::End, Line.LineNumber());
						}
						else
						{
							At(Emit(EDnaOpCode::GoTo, Line.LineNumber())).Name = FName(*Line.Payload.Target);
						}
						++Index;
						break;
					case EDnaLineKind::Command:
						CompileCommand(Line);
						++Index;
						break;
					default:
						++Index;
						break;
					}
				}
			}

			void CompileTextLine(const FDnaLineNode& Line)
			{
				const FDnaLinePayload& P = Line.Payload;
				FDnaLineData& Out = Data->Lines.AddDefaulted_GetRef();
				++LineOrdinal;
				Out.Id = P.Modifiers.Id.IsEmpty()
					? FName(*FString::Printf(TEXT("%s.%d"), *Data->Name.ToString(), LineOrdinal))
					: FName(*P.Modifiers.Id);
				Out.bExplicitId = !P.Modifiers.Id.IsEmpty();
				Out.Speaker = Line.Kind == EDnaLineKind::Speech ? FName(*P.Name) : NAME_None;
				for (const FString& Tag : P.SpeakerTags) { Out.Tags.Add(FName(*Tag)); }

				FTextBuilder Builder;
				Builder.Append(P.Text, Exprs);
				for (const FDnaLineNodePtr& Child : Line.Children)
				{
					if (Child.IsValid() && Child->Kind == EDnaLineKind::Continuation)
					{
						Builder.Pattern += TEXT("\n");
						Builder.Append(Child->Payload.Text, Exprs);
					}
				}
				Out.Text = MakeLocalizable(Namespace, Out.Id.ToString(), Builder.Pattern);
				Out.TextArgs = MoveTemp(Builder.Args);
				Out.Condition = Exprs.Compile(P.Modifiers.Condition);
				Out.Duration = P.Modifiers.bHasDuration ? static_cast<float>(P.Modifiers.DurationSeconds) : 0.f;
				Out.Stage = P.Modifiers.Stage.IsEmpty() ? NAME_None : FName(*P.Modifiers.Stage);
				Out.SourceLine = Line.LineNumber();
				if (!P.Modifiers.Voice.IsEmpty()) { VoiceOverrides.Add(Out.Id, P.Modifiers.Voice); }

				At(Emit(EDnaOpCode::Line, Line.LineNumber())).Index = Data->Lines.Num() - 1;
			}

			void CompileChoiceGroup(const TArray<FDnaLineNode*>& Lines, int32 Start, int32 End)
			{
				const int32 GroupIndex = Data->ChoiceGroups.AddDefaulted();
				At(Emit(EDnaOpCode::Choice, Lines[Start]->LineNumber())).Index = GroupIndex;

				TArray<int32> BodyJumps;
				for (int32 Index = Start; Index < End; ++Index)
				{
					const FDnaLineNode& Line = *Lines[Index];
					const FDnaLinePayload& P = Line.Payload;

					FDnaChoiceData Option;
					++ChoiceOrdinal;
					Option.Id = P.Modifiers.Id.IsEmpty()
						? FName(*FString::Printf(TEXT("%s.c%d"), *Data->Name.ToString(), ChoiceOrdinal))
						: FName(*P.Modifiers.Id);
					Option.bExplicitId = !P.Modifiers.Id.IsEmpty();
					FTextBuilder Builder;
					Builder.Append(P.Text, Exprs);
					Option.Label = MakeLocalizable(Namespace, Option.Id.ToString(), Builder.Pattern);
					Option.LabelArgs = MoveTemp(Builder.Args);
					Option.Condition = Exprs.Compile(P.Modifiers.Condition);
					Option.bOnce = P.Modifiers.bOnce;
					Option.SourceLine = Line.LineNumber();
					Option.Target = Data->Code.Num();

					TArray<FDnaLineNode*> Body;
					Line.GetContentChildren(Body);
					CompileBlock(Body);
					BodyJumps.Add(Emit(EDnaOpCode::Jump, Line.LineNumber()));

					Data->ChoiceGroups[GroupIndex].Options.Add(MoveTemp(Option));
				}

				const int32 Gather = Data->Code.Num();
				for (const int32 Jump : BodyJumps)
				{
					At(Jump).Index = Gather;
				}
				Data->ChoiceGroups[GroupIndex].Gather = Gather;
			}

			void CompileIfChain(const TArray<FDnaLineNode*>& Lines, int32 Start, int32 End)
			{
				TArray<int32> EndJumps;
				for (int32 Index = Start; Index < End; ++Index)
				{
					const FDnaLineNode& Line = *Lines[Index];
					int32 SkipJump = INDEX_NONE;
					if (Line.Kind != EDnaLineKind::Else)
					{
						SkipJump = Emit(EDnaOpCode::JumpIfFalse, Line.LineNumber());
						At(SkipJump).Expr = Exprs.Compile(Line.Payload.Condition);
					}
					TArray<FDnaLineNode*> Body;
					Line.GetContentChildren(Body);
					CompileBlock(Body);
					if (Index + 1 < End)
					{
						EndJumps.Add(Emit(EDnaOpCode::Jump, Line.LineNumber()));
					}
					if (SkipJump != INDEX_NONE)
					{
						At(SkipJump).Index = Data->Code.Num();
					}
				}
				for (const int32 Jump : EndJumps)
				{
					At(Jump).Index = Data->Code.Num();
				}
			}

			void CompileCommand(const FDnaLineNode& Line)
			{
				AppendCommand(Data->Code, Line, Exprs, Program);
			}
		};

		bool GenerateDocument(const FDnaProgram& Program, FDnaDocument& Document, const FDnaGenerateOptions& Options, FVoiceContext& Voices, FDnaGenerateResult& Result, FDnaDiagnosticSink& Diagnostics)
		{
			Diagnostics.SetFilePath(Document.Path);
			bool bOk = true;

			const FString BaseName = FPaths::GetBaseFilename(Document.Path);
			UPackage* Package = nullptr;
			FName ObjectName(*BaseName);
			if (Options.bTransient)
			{
				Package = GetTransientPackage();
				ObjectName = MakeUniqueObjectName(Package, UDreamNarrativeScript::StaticClass(), ObjectName);
			}
			else
			{
				const FString PackageName = GetDnaPackageName(Document.Path, Options.AssetDirectory);
				Package = CreatePackage(*PackageName);
				if (!Package)
				{
					return Diagnostics.Error(TEXT("DNS8001"), FDnaSpan(),
						FText::Format(LOCTEXT("NoPackage", "Cannot create package '{0}'"), FText::FromString(PackageName)));
				}
				Package->FullyLoad();
			}

			UDreamNarrativeScript* Script = FindObject<UDreamNarrativeScript>(Package, *ObjectName.ToString());
			if (!Script)
			{
				Script = NewObject<UDreamNarrativeScript>(Package, ObjectName, Options.bTransient ? RF_Transient : (RF_Public | RF_Standalone));
			}
			Script->Facts.Reset();
			Script->Characters.Reset();
			Script->Chapters.Reset();
			Script->Events.Reset();
			Script->Expressions.Reset();
			Script->Sections.Reset();
			Script->AssetReferences.Reset();
			Script->SourcePath = Document.Path;
			Script->SourceText = Document.Source.GetText();
			Script->SourceHash = UDreamNarrativeScript::HashSource(Script->SourceText);
			Script->SchemaVersion = 1;

			FDnaExprCompiler Exprs(Program, *Script);
			const FString Namespace = TEXT("DNA/") + BaseName;

			// -- declarations that belong to this document
			for (const TPair<FString, FDnaCharacterInfo>& Pair : Program.Characters)
			{
				const FDnaCharacterInfo& Info = Pair.Value;
				if (Info.Document != &Document) { continue; }
				FDnaCharacterDecl& Decl = Script->Characters.AddDefaulted_GetRef();
				Decl.Name = FName(*Info.Name);
				Decl.Display = Info.Display.IsEmpty() ? FText::FromString(Info.Name) : MakeLocalizable(Namespace, TEXT("character.") + Info.Name, Info.Display);
				Decl.bPlayer = Info.bPlayer;
				Decl.VoiceRoot = Info.VoiceRoot;
				for (const TPair<FString, FString>& Meta : Info.Meta) { Decl.Meta.Add(FName(*Meta.Key), Meta.Value); }
				Decl.WasName = Info.WasName.IsEmpty() ? NAME_None : FName(*Info.WasName);
			}
			for (const TPair<FString, FDnaFactInfo>& Pair : Program.Facts)
			{
				const FDnaFactInfo& Info = Pair.Value;
				if (Info.Document != &Document) { continue; }
				FDnaFactDecl& Decl = Script->Facts.AddDefaulted_GetRef();
				Decl.Name = FName(*Info.Name);
				Decl.Type = Info.Type;
				Decl.Owner = Info.bExtern ? EDnaFactOwner::Extern : EDnaFactOwner::Narrative;
				Decl.Authority = Info.Authority;
				Decl.bSession = Info.bSession;
				Decl.bProfile = Info.bProfile;
				Decl.bProgress = Info.bProgress;
				Decl.bHasRange = Info.bHasRange;
				Decl.RangeMin = Info.RangeMin;
				Decl.RangeMax = Info.RangeMax;
				for (const FString& Member : Info.Domain) { Decl.Domain.Add(FName(*Member)); }
				Decl.WasName = Info.WasName.IsEmpty() ? NAME_None : FName(*Info.WasName);
				Decl.GameplayTag = Info.GameplayTag.IsEmpty() ? NAME_None : FName(*Info.GameplayTag);
				Decl.Default = FDnaValue::DefaultFor(Info.Type);
				if (Info.DefaultExpr.IsValid())
				{
					FDnaValue Value;
					if (Exprs.Constant(Info.DefaultExpr, &Info, Value))
					{
						Decl.Default = Value;
						Decl.Default.Type = Info.Type;
						if (Info.Type == EDnaFactType::Int) { Decl.Default.Int = static_cast<int32>(Value.AsNumber()); }
						if (Info.Type == EDnaFactType::Float) { Decl.Default.Float = static_cast<float>(Value.AsNumber()); }
					}
					else
					{
						bOk = Diagnostics.Error(TEXT("DNS4006"), Info.DefaultExpr->Span,
							FText::Format(LOCTEXT("DefaultNotConstant", "The default of '{0}' must be a constant"), FText::FromString(Info.Name)));
					}
				}
				else if (Info.Type == EDnaFactType::Tag && Info.Domain.Num() > 0)
				{
					Decl.Default = FDnaValue::FromTag(FName(*Info.Domain[0]));
				}
			}
			for (const TPair<FString, FDnaEventInfo>& Pair : Program.Events)
			{
				const FDnaEventInfo& Info = Pair.Value;
				if (Info.Document != &Document) { continue; }
				FDnaEventDecl& Decl = Script->Events.AddDefaulted_GetRef();
				Decl.Name = FName(*Info.Name);
				Decl.GameplayTag = Info.GameplayTag.IsEmpty() ? NAME_None : FName(*Info.GameplayTag);
				for (const FDnaFactInfo& Param : Info.Params)
				{
					FDnaEventParamSource& Source = Decl.ParamSources.AddDefaulted_GetRef();
					Source.Source = Param.Source;
					Source.Tag = Param.SourceTag.IsEmpty() ? NAME_None : FName(*Param.SourceTag);
					FDnaFactDecl& Out = Decl.Params.AddDefaulted_GetRef();
					Out.Name = FName(*Param.Name);
					Out.Type = Param.Type;
					Out.bHasRange = Param.bHasRange;
					Out.RangeMin = Param.RangeMin;
					Out.RangeMax = Param.RangeMax;
					for (const FString& Member : Param.Domain) { Out.Domain.Add(FName(*Member)); }
					Out.Default = FDnaValue::DefaultFor(Param.Type);
					FDnaValue Value;
					if (Param.DefaultExpr.IsValid())
					{
						if (Exprs.Constant(Param.DefaultExpr, &Param, Value))
						{
							Out.Default = Value;
							Out.Default.Type = Param.Type;
							if (Param.Type == EDnaFactType::Int) { Out.Default.Int = static_cast<int32>(Value.AsNumber()); }
							if (Param.Type == EDnaFactType::Float) { Out.Default.Float = static_cast<float>(Value.AsNumber()); }
						}
						else
						{
							bOk = Diagnostics.Error(TEXT("DNS4006"), Param.DefaultExpr->Span,
								FText::Format(LOCTEXT("ParamDefaultNotConstant", "The default of parameter '{0}' must be a constant"), FText::FromString(Param.Name)));
						}
					}
					else if (Param.Type == EDnaFactType::Tag && Param.Domain.Num() > 0)
					{
						Out.Default = FDnaValue::FromTag(FName(*Param.Domain[0]));
					}
				}
			}
			for (const FString& ChapterName : Program.ChapterOrder)
			{
				const FDnaChapterInfo* Info = Program.Chapters.Find(ChapterName);
				if (!Info || Info->Document != &Document) { continue; }
				FDnaChapterDecl& Decl = Script->Chapters.AddDefaulted_GetRef();
				Decl.Name = FName(*Info->Name);
				Decl.After = Info->After.IsEmpty() ? NAME_None : FName(*Info->After);
				Decl.Title = Info->Title.IsEmpty() ? FText::FromString(Info->Name) : MakeLocalizable(Namespace, TEXT("chapter.") + Info->Name, Info->Title);
				Decl.WasName = Info->WasName.IsEmpty() ? NAME_None : FName(*Info->WasName);

				TArray<FDnaLineNode*> Lines;
				Info->Node->GetContentChildren(Lines);
				for (const FDnaLineNode* Line : Lines)
				{
					if (Line->Kind == EDnaLineKind::Implied)
					{
						TArray<FDnaLineNode*> Implications;
						Line->GetContentChildren(Implications);
						for (const FDnaLineNode* Implication : Implications)
						{
							if (Implication->Kind != EDnaLineKind::Implication) { continue; }
							const FDnaFactInfo* Fact = Program.FindFact(Implication->Payload.Name);
							FDnaValue Value;
							if (!Fact || !Exprs.Constant(Implication->Payload.Expr, Fact, Value))
							{
								bOk = Diagnostics.Error(TEXT("DNS4006"), Implication->Raw.Span,
									LOCTEXT("ImpliedNotConstant", "An implied fact must be a constant"));
								continue;
							}
							FDnaImpliedFact& Implied = Decl.Implied.AddDefaulted_GetRef();
							Implied.Fact = FName(*Fact->Name);
							Implied.Value = Value;
							Implied.Value.Type = Fact->Type;
						}
					}
					else if (Line->Kind == EDnaLineKind::Advance)
					{
						FDnaAdvanceRule& Rule = Decl.Advance.AddDefaulted_GetRef();
						Rule.Target = FName(*Line->Payload.Target);
						Rule.ConditionExpr = Exprs.Compile(Line->Payload.Condition);
					}
				}
			}

			// -- quests: the rules in a section, the state in two facts each
			TArray<FDnaLineNode*> QuestRoots;
			Document.GetRootsOfKind(EDnaLineKind::Quest, QuestRoots);
			if (QuestRoots.Num() > 0)
			{
				UDnaQuestSection* Section = NewObject<UDnaQuestSection>(Script, NAME_None, Options.bTransient ? RF_Transient : RF_NoFlags);
				for (const FDnaLineNode* Root : QuestRoots)
				{
					FDnaQuestData& Quest = Section->Quests.AddDefaulted_GetRef();
					CompileQuest(*Root, Quest, Exprs, Program, Namespace);

					FDnaFactDecl& State = Script->Facts.AddDefaulted_GetRef();
					State.Name = ::DreamNarrative::QuestStateFact(Quest.Name);
					State.Type = EDnaFactType::Tag;
					State.Owner = EDnaFactOwner::Narrative;
					for (const EDnaQuestState Each : { EDnaQuestState::Inactive, EDnaQuestState::Accepted, EDnaQuestState::Going,
						EDnaQuestState::Completed, EDnaQuestState::Failed, EDnaQuestState::TimedOut })
					{
						State.Domain.Add(::DreamNarrative::QuestStateMember(Each));
					}
					State.Default = FDnaValue::FromTag(::DreamNarrative::QuestStateMember(EDnaQuestState::Inactive));
					State.WasName = Quest.WasName.IsNone() ? NAME_None : ::DreamNarrative::QuestStateFact(Quest.WasName);
					// A side quest is progress: selecting a chapter keeps where it stands.
					State.bProgress = Quest.Priority == EDnaQuestPriority::Side;

					FDnaFactDecl& Done = Script->Facts.AddDefaulted_GetRef();
					Done.Name = ::DreamNarrative::QuestObjectivesFact(Quest.Name);
					Done.Type = EDnaFactType::Int;
					Done.Owner = EDnaFactOwner::Narrative;
					Done.Default = FDnaValue::FromInt(0);
					Done.WasName = Quest.WasName.IsNone() ? NAME_None : ::DreamNarrative::QuestObjectivesFact(Quest.WasName);
					Done.bProgress = State.bProgress;
				}
				Script->Sections.Add(Section);
			}

			// -- archives: the texts in a section, what is unlocked in a fact each
			TArray<FDnaLineNode*> ArchiveRoots;
			Document.GetRootsOfKind(EDnaLineKind::Archive, ArchiveRoots);
			if (ArchiveRoots.Num() > 0)
			{
				UDnaArchiveSection* Section = NewObject<UDnaArchiveSection>(Script, NAME_None, Options.bTransient ? RF_Transient : RF_NoFlags);
				for (const FDnaLineNode* Root : ArchiveRoots)
				{
					const FDnaArchiveInfo* Info = Program.Archives.Find(Root->Payload.Name);
					if (!Info || Info->Node != Root) { continue; }
					FDnaArchiveData& Archive = Section->Archives.AddDefaulted_GetRef();
					CompileArchive(*Root, Archive, Exprs, Namespace);

					FDnaFactDecl& Unlocked = Script->Facts.AddDefaulted_GetRef();
					Unlocked.Name = ::DreamNarrative::ArchiveFact(Archive.Name);
					Unlocked.Type = EDnaFactType::Int;
					Unlocked.Owner = EDnaFactOwner::Narrative;
					Unlocked.Default = FDnaValue::FromInt(0);
					Unlocked.bProfile = Archive.bProfile;
				}
				Script->Sections.Add(Section);
			}

			// -- stages: the timeline of each, its texts localisable
			TArray<FDnaLineNode*> StageRoots;
			Document.GetRootsOfKind(EDnaLineKind::Stage, StageRoots);
			if (StageRoots.Num() > 0)
			{
				UDnaStageSection* Section = NewObject<UDnaStageSection>(Script, NAME_None, Options.bTransient ? RF_Transient : RF_NoFlags);
				for (const FDnaLineNode* Root : StageRoots)
				{
					const FDnaStageInfo* Info = Program.Stages.Find(Root->Payload.Name);
					if (!Info || Info->Node != Root) { continue; }
					// The validator has said what is wrong with it already.
					FDnaDiagnosticSink Checked;
					FDnaStageData& Stage = Section->Stages.AddDefaulted_GetRef();
					CompileDnaStage(*Root, Stage, Checked);
					int32 TextIndex = 0;
					for (FDnaStageEntry& Entry : Stage.Entries)
					{
						if (Entry.Verb == EDnaStageVerb::Text)
						{
							Entry.Text = MakeLocalizable(Namespace, FString::Printf(TEXT("stage.%s.text%d"), *Info->Name, TextIndex++), Entry.Text.ToString());
						}
						ResolveStageAsset(Entry, Info->Name, !Options.bTransient, Script->AssetReferences, Diagnostics);
					}
				}
				Script->Sections.Add(Section);
			}

			// -- dialogue, and the channels it runs on
			TArray<FDnaLineNode*> DialogueRoots;
			Document.GetRootsOfKind(EDnaLineKind::Dialogue, DialogueRoots);
			TArray<FDnaLineNode*> ChannelRoots;
			Document.GetRootsOfKind(EDnaLineKind::Channel, ChannelRoots);
			if (DialogueRoots.Num() > 0 || ChannelRoots.Num() > 0)
			{
				UDnaDialogueSection* Section = NewObject<UDnaDialogueSection>(Script, NAME_None, Options.bTransient ? RF_Transient : RF_NoFlags);
				for (const FDnaLineNode* Root : ChannelRoots)
				{
					const FDnaChannelInfo* Info = Program.FindChannel(Root->Payload.Name);
					if (!Info || Info->Node != Root) { continue; }
					FDnaChannelData& Channel = Section->Channels.AddDefaulted_GetRef();
					Channel.Name = FName(*Info->Name);
					Channel.bLockInput = Info->bLockInput;
					Channel.bAutoAdvance = Info->bAutoAdvance;
					Channel.Busy = Info->Busy == TEXT("interrupt") ? EDnaChannelBusy::Interrupt : Info->Busy == TEXT("drop") ? EDnaChannelBusy::Drop : EDnaChannelBusy::Queue;
					for (const FString& Other : Info->Pauses) { Channel.Pauses.Add(FName(*Other)); }
					for (const FString& Other : Info->Stops) { Channel.Stops.Add(FName(*Other)); }
					Channel.bPauseDuringStage = Info->bPauseDuringStage;
					Channel.SourceLine = Root->LineNumber();
				}
				FDialogueCompiler Compiler(Exprs, Program, Namespace);
				for (const FDnaLineNode* Root : DialogueRoots)
				{
					FDnaDialogueNodeData& Node = Section->Nodes.AddDefaulted_GetRef();
					Compiler.CompileNode(*Root, Node);
				}
				ResolveVoices(*Section, Compiler.VoiceOverrides, Program, Voices, Script->AssetReferences, Diagnostics);
				Script->Sections.Add(Section);
			}

			Result.Scripts.Add(Script);

#if WITH_EDITOR
			if (Options.bSave && !Options.bTransient && bOk)
			{
				Package->MarkPackageDirty();
				const FString FileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
				FSavePackageArgs SaveArgs;
				SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
				SaveArgs.SaveFlags = SAVE_NoError;
				if (!UPackage::SavePackage(Package, Script, *FileName, SaveArgs))
				{
					return Diagnostics.Error(TEXT("DNS8001"), FDnaSpan(),
						FText::Format(LOCTEXT("SaveFailed", "Cannot save '{0}'"), FText::FromString(FileName)));
				}
				FAssetRegistryModule::AssetCreated(Script);
				++Result.NumSaved;
				UE_LOG(LogDreamNarrativeCompiler, Display, TEXT("Wrote %s"), *FileName);
			}
#endif
			return bOk;
		}
	}

	int32 CompileDnaExpression(const FDnaProgram& Program, const FDnaExprPtr& Expr, UDreamNarrativeScript& Pool)
	{
		FDnaExprCompiler Compiler(Program, Pool);
		return Compiler.Compile(Expr);
	}

	int32 CompileDnaValueFor(const FDnaProgram& Program, const FDnaExprPtr& Expr, const FString& TargetFact, UDreamNarrativeScript& Pool)
	{
		FDnaExprCompiler Compiler(Program, Pool);
		return Compiler.CompileValueFor(Expr, Program.FindFact(TargetFact));
	}

	int32 CompileDnaValueForParam(const FDnaProgram& Program, const FDnaExprPtr& Expr, const FString& Event, const FString& Param, UDreamNarrativeScript& Pool)
	{
		const FDnaEventInfo* Info = Program.FindEvent(Event);
		FDnaExprCompiler Compiler(Program, Pool);
		return Compiler.CompileValueFor(Expr, Info ? Info->FindParam(Param) : nullptr);
	}

	bool IsStaleDnaSource(const FString& Recorded, const FString& Current)
	{
		if (FPaths::IsSamePath(Recorded, Current))
		{
			return false;
		}
		// Recorded paths are absolute. One relative to nothing (a test's) only ever names a file of this run.
		if (FPaths::IsRelative(Recorded))
		{
			return false;
		}
		FString Root = GetDnaSourceRoot();
		FPaths::NormalizeDirectoryName(Root);
		FString Normalized = FPaths::ConvertRelativePathToFull(Recorded);
		FPaths::NormalizeFilename(Normalized);
		const bool bInTree = Normalized.StartsWith(Root + TEXT("/"), ESearchCase::IgnoreCase);
		return !bInTree || !FPaths::FileExists(Normalized);
	}

	bool GenerateDnaProgram(const FDnaProgram& Program, const FDnaGenerateOptions& Options, FDnaGenerateResult& OutResult, FDnaDiagnosticSink& Diagnostics)
	{
		// Preflight every output before mutating assets; filenames are the legacy package keys.
		if (!Options.bTransient)
		{
			TMap<FName, FString> SourcesByPackage;
			for (const TSharedPtr<FDnaDocument>& Document : Program.Documents)
			{
				if (!Document.IsValid()) { continue; }
				const FString PackageName = GetDnaPackageName(Document->Path, Options.AssetDirectory);
				const FName PackageKey(*PackageName);
				const FString* OtherSource = SourcesByPackage.Find(PackageKey);
				FString ExistingSource;
				if (!OtherSource)
				{
					const FString ObjectPath = PackageName + TEXT(".") + FPaths::GetBaseFilename(Document->Path);
					UDreamNarrativeScript* Existing = FindObject<UDreamNarrativeScript>(nullptr, *ObjectPath);
					if (!Existing && FPackageName::DoesPackageExist(PackageName))
					{
						Existing = LoadObject<UDreamNarrativeScript>(nullptr, *ObjectPath);
					}
					if (Existing) { ExistingSource = Existing->SourcePath; OtherSource = &ExistingSource; }
				}
				if (OtherSource && !OtherSource->IsEmpty() && !SourcesByPackage.Contains(PackageKey) && IsStaleDnaSource(*OtherSource, Document->Path))
				{
					// The asset came from this file in another checkout, or from a file that has since moved away.
					Diagnostics.Warning(TEXT("DNS8001"), FDnaSpan(),
						FText::Format(LOCTEXT("PackageTakeover", "'{0}' takes over asset '{1}', last generated from '{2}', which is not in this source tree any more."),
							FText::FromString(Document->Path), FText::FromString(PackageName), FText::FromString(*OtherSource)));
					OtherSource = nullptr;
				}
				if (OtherSource && !FPaths::IsSamePath(*OtherSource, Document->Path))
				{
					return Diagnostics.Error(TEXT("DNS8001"), FDnaSpan(),
						FText::Format(LOCTEXT("PackageCollision", "'{0}' and '{1}' map to the same asset '{2}'. Rename one source file; no assets were generated."),
							FText::FromString(*OtherSource), FText::FromString(Document->Path), FText::FromString(PackageName)));
				}
				SourcesByPackage.Add(PackageKey, Document->Path);
			}
		}
		// The voice tables, read once for the whole program.
		FVoiceContext Voices;
		{
			TMap<FString, FString> ProjectCsv;
			if (!Options.VoiceCsvTexts)
			{
				TArray<FString> Files;
				FindDnaVoiceCsvFiles(GetDnaSourceRoot(), Files);
				for (const FString& File : Files)
				{
					FString Text;
					if (FFileHelper::LoadFileToString(Text, *File)) { ProjectCsv.Add(File, MoveTemp(Text)); }
				}
			}
			ReadDnaVoiceTables(Options.VoiceCsvTexts ? *Options.VoiceCsvTexts : ProjectCsv, Options.bReadVoiceDataTables, Voices.Entries, Diagnostics);
		}
		bool bOk = !Diagnostics.HasErrors();
		for (const TSharedPtr<FDnaDocument>& Document : Program.Documents)
		{
			if (Document.IsValid())
			{
				bOk &= GenerateDocument(Program, *Document, Options, Voices, OutResult, Diagnostics);
			}
		}
		if (Options.bReportVoiceOrphans)
		{
			for (const TPair<FString, FDnaVoiceEntry>& Pair : Voices.Entries)
			{
				if (Voices.LineIds.Contains(Pair.Key)) { continue; }
				FDnaSpan Span;
				Span.Line = FMath::Max(1, Pair.Value.Line);
				Diagnostics.SetFilePath(Pair.Value.Source);
				Diagnostics.Warning(TEXT("DNS6004"), Span, FText::Format(LOCTEXT("VoiceOrphan", "The voice table answers '{0}', but no line has that id"), FText::FromString(Pair.Key)));
			}
		}
		return bOk;
	}
}

#undef LOCTEXT_NAMESPACE
