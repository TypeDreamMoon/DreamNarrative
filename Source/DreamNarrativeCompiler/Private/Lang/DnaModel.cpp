// Copyright DreamMoon. All Rights Reserved.

#include "Lang/DnaModel.h"
#include "Lang/DnaLexer.h"

#define LOCTEXT_NAMESPACE "DreamNarrativeLang"

namespace UE::DreamNarrative::Lang
{
	bool FDnaProgram::IsDeclared(const FString& Name, FString* OutKind) const
	{
		if (Facts.Contains(Name)) { if (OutKind) { *OutKind = TEXT("fact"); } return true; }
		if (Characters.Contains(Name)) { if (OutKind) { *OutKind = TEXT("character"); } return true; }
		if (Chapters.Contains(Name)) { if (OutKind) { *OutKind = TEXT("chapter"); } return true; }
		if (Quests.Contains(Name)) { if (OutKind) { *OutKind = TEXT("quest"); } return true; }
		if (Dialogues.Contains(Name)) { if (OutKind) { *OutKind = TEXT("dialogue"); } return true; }
		if (Stages.Contains(Name)) { if (OutKind) { *OutKind = TEXT("stage"); } return true; }
		return false;
	}

	namespace
	{
		struct FBuilder
		{
			FDnaProgram& Program;
			FDnaDiagnosticSink& Diagnostics;
			bool bOk = true;
			/** Every `was` seen, checked once all declarations are in. */
			TArray<TPair<FString, FDnaLineNode*>> Renames;

			FBuilder(FDnaProgram& InProgram, FDnaDiagnosticSink& InDiagnostics)
				: Program(InProgram), Diagnostics(InDiagnostics)
			{
			}

			/** DNS3004 / DNS3008 for a new name; true when the name may be added. */
			bool Admit(const FString& Name, const TCHAR* Kind, FDnaLineNode& Node)
			{
				if (IsDnaReservedWord(Name))
				{
					bOk = Diagnostics.Error(TEXT("DNS3008"), Node.Raw.Span,
						FText::Format(LOCTEXT("ReservedName", "'{0}' is a reserved word and cannot name a {1}"),
							FText::FromString(Name), FText::FromString(Kind)));
					return false;
				}
				FString ExistingKind;
				if (Program.IsDeclared(Name, &ExistingKind))
				{
					bOk = Diagnostics.Error(TEXT("DNS3004"), Node.Raw.Span,
						FText::Format(LOCTEXT("DuplicateName", "'{0}' is already declared as a {1}; names are unique across the project"),
							FText::FromString(Name), FText::FromString(ExistingKind)));
					return false;
				}
				return true;
			}

			void NoteRename(const FString& WasName, FDnaLineNode& Node)
			{
				if (!WasName.IsEmpty())
				{
					Renames.Emplace(WasName, &Node);
				}
			}

			void FillFact(FDnaFactInfo& Info, const FString& FullName, FDnaLineNode& Node, FDnaDocument& Document, const FDnaLinePayload* Group)
			{
				const FDnaLinePayload& P = Node.Payload;
				Info.Name = FullName;
				Info.WasName = P.WasName;
				Info.Node = &Node;
				Info.Document = &Document;
				Info.Type = P.FactType;
				Info.bExtern = P.bExtern || (Group && Group->bExtern);
				Info.bSession = P.bSession || (Group && Group->bSession);
				Info.Authority = (Group && P.Authority == EDnaAuthority::Shared) ? Group->Authority : P.Authority;
				Info.bHasRange = P.bHasRange;
				Info.RangeMin = P.RangeMin;
				Info.RangeMax = P.RangeMax;
				Info.Domain = P.Domain;
				Info.DefaultExpr = P.DefaultExpr;
			}

			void AddDocument(const TSharedPtr<FDnaDocument>& DocumentPtr)
			{
				FDnaDocument& Document = *DocumentPtr;
				Diagnostics.SetFilePath(Document.Path);

				for (const FDnaLineNodePtr& RootPtr : Document.Roots)
				{
					if (!RootPtr.IsValid() || RootPtr->IsFloating()) { continue; }
					FDnaLineNode& Root = *RootPtr;
					const FDnaLinePayload& P = Root.Payload;

					switch (Root.Kind)
					{
					case EDnaLineKind::Character:
					{
						if (!Admit(P.Name, TEXT("character"), Root)) { break; }
						FDnaCharacterInfo& Info = Program.Characters.Add(P.Name);
						Info.Name = P.Name;
						Info.WasName = P.WasName;
						Info.Node = &Root;
						Info.Document = &Document;
						Info.bPlayer = P.bPlayer;
						TArray<FDnaLineNode*> Properties;
						Root.GetContentChildren(Properties);
						for (const FDnaLineNode* Property : Properties)
						{
							if (Property->Kind != EDnaLineKind::Property) { continue; }
							if (Property->Payload.Name == TEXT("display")) { Info.Display = Property->Payload.Value; }
							else if (Property->Payload.Name == TEXT("voice")) { Info.VoiceRoot = Property->Payload.Value; }
							else { Info.Meta.Add(Property->Payload.Name, Property->Payload.Value); }
						}
						NoteRename(P.WasName, Root);
						break;
					}
					case EDnaLineKind::Fact:
					{
						if (!Admit(P.Name, TEXT("fact"), Root)) { break; }
						FDnaFactInfo& Info = Program.Facts.Add(P.Name);
						FillFact(Info, P.Name, Root, Document, nullptr);
						NoteRename(P.WasName, Root);
						break;
					}
					case EDnaLineKind::FactGroup:
					{
						if (!Admit(P.Name, TEXT("fact group"), Root)) { break; }
						TArray<FDnaLineNode*> Entries;
						Root.GetContentChildren(Entries);
						for (FDnaLineNode* Entry : Entries)
						{
							if (Entry->Kind != EDnaLineKind::Fact) { continue; }
							const FString FullName = P.Name + TEXT(".") + Entry->Payload.Name;
							if (!Admit(FullName, TEXT("fact"), *Entry)) { continue; }
							FDnaFactInfo& Info = Program.Facts.Add(FullName);
							FillFact(Info, FullName, *Entry, Document, &P);
							NoteRename(Entry->Payload.WasName, *Entry);
						}
						break;
					}
					case EDnaLineKind::Chapter:
					{
						if (!Admit(P.Name, TEXT("chapter"), Root)) { break; }
						FDnaChapterInfo& Info = Program.Chapters.Add(P.Name);
						Info.Name = P.Name;
						Info.WasName = P.WasName;
						Info.Node = &Root;
						Info.Document = &Document;
						Info.After = P.Target;
						TArray<FDnaLineNode*> Lines;
						Root.GetContentChildren(Lines);
						for (const FDnaLineNode* Line : Lines)
						{
							if (Line->Kind == EDnaLineKind::Property && Line->Payload.Name == TEXT("title")) { Info.Title = Line->Payload.Value; }
						}
						Program.ChapterOrder.Add(P.Name);
						NoteRename(P.WasName, Root);
						break;
					}
					case EDnaLineKind::Quest:
					{
						if (!Admit(P.Name, TEXT("quest"), Root)) { break; }
						FDnaQuestInfo& Info = Program.Quests.Add(P.Name);
						Info.Name = P.Name;
						Info.WasName = P.WasName;
						Info.Node = &Root;
						Info.Document = &Document;
						NoteRename(P.WasName, Root);
						break;
					}
					case EDnaLineKind::Dialogue:
					{
						if (!Admit(P.Name, TEXT("dialogue"), Root)) { break; }
						FDnaDialogueInfo& Info = Program.Dialogues.Add(P.Name);
						Info.Name = P.Name;
						Info.WasName = P.WasName;
						Info.Node = &Root;
						Info.Document = &Document;
						Info.ForCharacter = P.ForCharacter;
						Info.Condition = P.Condition;
						Info.Priority = P.Priority;
						NoteRename(P.WasName, Root);
						break;
					}
					case EDnaLineKind::Stage:
					{
						if (!Admit(P.Name, TEXT("stage"), Root)) { break; }
						FDnaStageInfo& Info = Program.Stages.Add(P.Name);
						Info.Name = P.Name;
						Info.WasName = P.WasName;
						Info.Node = &Root;
						Info.Document = &Document;
						TArray<FDnaLineNode*> Lines;
						Root.GetContentChildren(Lines);
						for (const FDnaLineNode* Line : Lines)
						{
							if (Line->Kind == EDnaLineKind::Property && Line->Payload.Name == TEXT("base") && Line->Payload.Value == TEXT("line"))
							{
								Info.bLineRelative = true;
							}
							if (Line->Kind == EDnaLineKind::Duration)
							{
								Info.DurationSeconds = Line->Payload.Seconds;
							}
						}
						NoteRename(P.WasName, Root);
						break;
					}
					case EDnaLineKind::Test:
					case EDnaLineKind::Explore:
					{
						if (P.Name.IsEmpty()) { break; }
						if (Program.Tests.Contains(P.Name) || Program.Explores.Contains(P.Name))
						{
							bOk = Diagnostics.Error(TEXT("DNS3004"), Root.Raw.Span,
								FText::Format(LOCTEXT("DuplicateTest", "A test or explore block called '{0}' already exists; their names are unique"), FText::FromString(P.Name)));
							break;
						}
						FDnaTestInfo& Info = (Root.Kind == EDnaLineKind::Explore ? Program.Explores : Program.Tests).Add(P.Name);
						Info.Name = P.Name;
						Info.Node = &Root;
						Info.Document = &Document;
						break;
					}
					default:
						break;
					}
				}
			}

			void CheckRenames()
			{
				for (const TPair<FString, FDnaLineNode*>& Rename : Renames)
				{
					FString Kind;
					if (Program.IsDeclared(Rename.Key, &Kind))
					{
						bOk = Diagnostics.Error(TEXT("DNS3007"), Rename.Value->Raw.Span,
							FText::Format(LOCTEXT("WasStillDeclared", "'was {0}' names the old name, but a {1} called '{0}' is still declared"),
								FText::FromString(Rename.Key), FText::FromString(Kind)));
					}
				}
			}
		};
	}

	bool BuildDnaProgram(const TArray<TSharedPtr<FDnaDocument>>& Documents, FDnaProgram& OutProgram, FDnaDiagnosticSink& Diagnostics)
	{
		OutProgram.Documents = Documents;
		FBuilder Builder(OutProgram, Diagnostics);
		for (const TSharedPtr<FDnaDocument>& Document : Documents)
		{
			if (Document.IsValid())
			{
				Builder.AddDocument(Document);
			}
		}
		Builder.CheckRenames();
		return Builder.bOk;
	}
}

#undef LOCTEXT_NAMESPACE
