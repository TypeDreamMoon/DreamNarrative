// Copyright DreamMoon. All Rights Reserved.

#include "Lang/DnaValidator.h"

#define LOCTEXT_NAMESPACE "DreamNarrativeLang"

namespace UE::DreamNarrative::Lang
{
	FDnaTypeInfo FDnaTypeInfo::OfFact(const FDnaFactInfo& Fact)
	{
		FDnaTypeInfo Type;
		switch (Fact.Type)
		{
		case EDnaFactType::Bool: Type.Kind = EKind::Bool; break;
		case EDnaFactType::Int: Type.Kind = EKind::Int; break;
		case EDnaFactType::Float: Type.Kind = EKind::Float; break;
		case EDnaFactType::Tag: Type.Kind = EKind::Tag; Type.TagFact = &Fact; break;
		}
		return Type;
	}

	const TCHAR* FDnaTypeInfo::Describe() const
	{
		switch (Kind)
		{
		case EKind::Unknown: return TEXT("unknown");
		case EKind::Bool: return TEXT("bool");
		case EKind::Int: return TEXT("int");
		case EKind::Float: return TEXT("float");
		case EKind::Tag: return TEXT("tag");
		case EKind::Chapter: return TEXT("chapter");
		}
		return TEXT("?");
	}

	namespace
	{
		using EKind = FDnaTypeInfo::EKind;

		class FValidator
		{
		public:
			FValidator(const FDnaProgram& InProgram, FDnaDiagnosticSink& InDiagnostics)
				: Program(InProgram), Diagnostics(InDiagnostics)
			{
			}

			bool Run()
			{
				for (const TSharedPtr<FDnaDocument>& Document : Program.Documents)
				{
					if (!Document.IsValid()) { continue; }
					CurrentDocument = Document.Get();
					Diagnostics.SetFilePath(Document->Path);
					for (const FDnaLineNodePtr& Root : Document->Roots)
					{
						if (Root.IsValid() && !Root->IsFloating())
						{
							CheckRoot(*Root);
						}
					}
				}
				return bOk;
			}

			bool RunOnExpression(const FDnaExprPtr& Expr, bool bAsCondition)
			{
				if (!Expr.IsValid())
				{
					return false;
				}
				if (bAsCondition)
				{
					CheckCondition(Expr);
				}
				else
				{
					CheckExpr(*Expr);
				}
				return bOk;
			}

		private:
			const FDnaProgram& Program;
			FDnaDiagnosticSink& Diagnostics;
			const FDnaDocument* CurrentDocument = nullptr;
			bool bOk = true;

			/** Where a line's modifier tail starts, for diagnostics about the modifiers. */
			FDnaSpan TailSpan(const FDnaModifiers& Mods) const
			{
				if (CurrentDocument && Mods.TailOffset != INDEX_NONE)
				{
					return CurrentDocument->Source.MakeSpan(Mods.TailOffset, 1);
				}
				return FDnaSpan();
			}

			bool Fail(const TCHAR* Code, const FDnaSpan& Span, const FText& Message)
			{
				bOk = false;
				return Diagnostics.Error(Code, Span, Message);
			}

			// ---------------------------------------------------------------- roots

			void CheckRoot(FDnaLineNode& Root)
			{
				const FDnaLinePayload& P = Root.Payload;
				switch (Root.Kind)
				{
				case EDnaLineKind::Fact:
					CheckFactDefault(Root, P.Name);
					break;
				case EDnaLineKind::FactGroup:
				{
					TArray<FDnaLineNode*> Entries;
					Root.GetContentChildren(Entries);
					for (FDnaLineNode* Entry : Entries)
					{
						if (Entry->Kind == EDnaLineKind::Fact)
						{
							CheckFactDefault(*Entry, P.Name + TEXT(".") + Entry->Payload.Name);
						}
					}
					break;
				}
				case EDnaLineKind::Chapter:
					CheckChapter(Root);
					break;
				case EDnaLineKind::Quest:
					CheckQuest(Root);
					break;
				case EDnaLineKind::Dialogue:
					CheckDialogue(Root);
					break;
				case EDnaLineKind::Stage:
					break;
				default:
					break;
				}
			}

			void CheckFactDefault(const FDnaLineNode& Node, const FString& FullName)
			{
				const FDnaFactInfo* Fact = Program.FindFact(FullName);
				if (!Fact || !Fact->DefaultExpr.IsValid())
				{
					return;
				}
				CheckAssignment(*Fact, Fact->DefaultExpr, Node.Raw.Span);
				if (Fact->Type == EDnaFactType::Tag && Fact->Domain.Num() == 0)
				{
					Fail(TEXT("DNS4002"), Node.Raw.Span,
						FText::Format(LOCTEXT("TagNoDomain", "Tag fact '{0}' declares no members; add `in (...)`"), FText::FromString(FullName)));
				}
			}

			void CheckChapter(FDnaLineNode& Root)
			{
				const FDnaLinePayload& P = Root.Payload;
				if (!P.Target.IsEmpty())
				{
					RequireChapter(P.Target, Root.Raw.Span);
				}
				TArray<FDnaLineNode*> Lines;
				Root.GetContentChildren(Lines);
				for (FDnaLineNode* Line : Lines)
				{
					if (Line->Kind == EDnaLineKind::Implied)
					{
						TArray<FDnaLineNode*> Implications;
						Line->GetContentChildren(Implications);
						for (const FDnaLineNode* Implication : Implications)
						{
							if (Implication->Kind != EDnaLineKind::Implication) { continue; }
							if (const FDnaFactInfo* Fact = RequireFact(Implication->Payload.Name, Implication->Raw.Span))
							{
								CheckAssignment(*Fact, Implication->Payload.Expr, Implication->Raw.Span);
							}
						}
					}
					else if (Line->Kind == EDnaLineKind::Advance)
					{
						RequireChapter(Line->Payload.Target, Line->Raw.Span);
						if (Line->Payload.Condition.IsValid()) { CheckCondition(Line->Payload.Condition); }
					}
				}
			}

			void CheckQuest(FDnaLineNode& Root)
			{
				TArray<FDnaLineNode*> Lines;
				Root.GetContentChildren(Lines);
				for (FDnaLineNode* Line : Lines)
				{
					const FDnaLinePayload& P = Line->Payload;
					switch (Line->Kind)
					{
					case EDnaLineKind::Available:
					case EDnaLineKind::FailIf:
					case EDnaLineKind::Objective:
						if (P.Condition.IsValid()) { CheckCondition(P.Condition); }
						if (Line->Kind == EDnaLineKind::Objective) { CheckTexts(*Line); }
						break;
					case EDnaLineKind::Journal:
						CheckTexts(*Line);
						break;
					case EDnaLineKind::On:
						CheckCommandBlock(*Line);
						break;
					case EDnaLineKind::Subquest:
						RequireQuest(P.Name, Line->Raw.Span);
						break;
					case EDnaLineKind::Property:
						CheckText(P.Text);
						break;
					default:
						break;
					}
				}
			}

			// ---------------------------------------------------------------- dialogue

			void CheckDialogue(FDnaLineNode& Root)
			{
				const FDnaLinePayload& P = Root.Payload;
				if (!P.ForCharacter.IsEmpty())
				{
					RequireCharacter(P.ForCharacter, Root.Raw.Span, /*bIsSpeaker=*/false);
				}
				if (P.Condition.IsValid())
				{
					CheckCondition(P.Condition);
				}
				CheckDialogueBlock(Root);
			}

			void CheckDialogueBlock(FDnaLineNode& Block)
			{
				TArray<FDnaLineNode*> Lines;
				Block.GetContentChildren(Lines);

				// else must follow an if; choice groups are runs of consecutive choices
				EDnaLineKind Previous = EDnaLineKind::Unknown;
				int32 GroupStart = INDEX_NONE;
				auto CloseGroup = [&](int32 EndExclusive)
				{
					if (GroupStart == INDEX_NONE) { return; }
					bool bHasFallback = false;
					for (int32 Index = GroupStart; Index < EndExclusive; ++Index)
					{
						const FDnaModifiers& Mods = Lines[Index]->Payload.Modifiers;
						if (!Mods.Condition.IsValid() && !Mods.bOnce)
						{
							bHasFallback = true;
							break;
						}
					}
					if (!bHasFallback)
					{
						Fail(TEXT("DNS5008"), Lines[GroupStart]->Raw.Span,
							LOCTEXT("ChoiceNoFallback", "Every option in this group is conditional or `once`; add one plain option so the group cannot run dry"));
					}
					GroupStart = INDEX_NONE;
				};

				for (int32 Index = 0; Index < Lines.Num(); ++Index)
				{
					FDnaLineNode& Line = *Lines[Index];
					if (Line.Kind == EDnaLineKind::Choice)
					{
						if (GroupStart == INDEX_NONE) { GroupStart = Index; }
					}
					else
					{
						CloseGroup(Index);
					}

					if ((Line.Kind == EDnaLineKind::Else || Line.Kind == EDnaLineKind::ElseIf)
						&& !(Previous == EDnaLineKind::If || Previous == EDnaLineKind::ElseIf))
					{
						Fail(TEXT("DNS2010"), Line.Raw.Span, LOCTEXT("ElseWithoutIf", "'else' has no 'if' directly before it"));
					}
					Previous = Line.Kind;
					CheckDialogueLine(Line);
				}
				CloseGroup(Lines.Num());
			}

			void CheckDialogueLine(FDnaLineNode& Line)
			{
				const FDnaLinePayload& P = Line.Payload;
				switch (Line.Kind)
				{
				case EDnaLineKind::Speech:
					RequireCharacter(P.Name, Line.Raw.Span, /*bIsSpeaker=*/true);
					CheckTextLine(Line);
					break;
				case EDnaLineKind::Narration:
					CheckTextLine(Line);
					break;
				case EDnaLineKind::Choice:
					CheckText(P.Text);
					CheckModifiers(P.Modifiers);
					CheckDialogueBlock(Line);
					break;
				case EDnaLineKind::If:
				case EDnaLineKind::ElseIf:
					if (P.Condition.IsValid()) { CheckCondition(P.Condition); }
					CheckDialogueBlock(Line);
					break;
				case EDnaLineKind::Else:
					CheckDialogueBlock(Line);
					break;
				case EDnaLineKind::Jump:
					if (!P.bEnd) { RequireDialogue(P.Target, Line.Raw.Span); }
					break;
				case EDnaLineKind::Command:
					CheckCommand(Line);
					break;
				default:
					break;
				}
			}

			void CheckTextLine(FDnaLineNode& Line)
			{
				CheckText(Line.Payload.Text);
				CheckModifiers(Line.Payload.Modifiers);
				for (const FDnaLineNodePtr& Child : Line.Children)
				{
					if (Child.IsValid() && Child->Kind == EDnaLineKind::Continuation)
					{
						CheckText(Child->Payload.Text);
					}
				}
			}

			void CheckTexts(FDnaLineNode& Block)
			{
				TArray<FDnaLineNode*> Lines;
				Block.GetContentChildren(Lines);
				for (const FDnaLineNode* Line : Lines)
				{
					if (Line->Kind == EDnaLineKind::Property) { CheckText(Line->Payload.Text); }
				}
			}

			void CheckText(const FDnaText& Text)
			{
				for (const FDnaTextPart& Part : Text.Parts)
				{
					if (Part.IsExpr())
					{
						CheckExpr(*Part.Expr);
					}
				}
			}

			void CheckModifiers(const FDnaModifiers& Mods)
			{
				if (Mods.Condition.IsValid())
				{
					CheckCondition(Mods.Condition);
				}
				if (!Mods.Stage.IsEmpty())
				{
					RequireStage(Mods.Stage, TailSpan(Mods));
				}
			}

			void CheckCommandBlock(FDnaLineNode& Block)
			{
				TArray<FDnaLineNode*> Lines;
				Block.GetContentChildren(Lines);
				for (FDnaLineNode* Line : Lines)
				{
					if (Line->Kind == EDnaLineKind::Command) { CheckCommand(*Line); }
				}
			}

			void CheckCommand(FDnaLineNode& Line)
			{
				const FDnaLinePayload& P = Line.Payload;
				switch (P.Command)
				{
				case EDnaCommandKind::Set:
				{
					const FDnaFactInfo* Fact = RequireFact(P.Name, Line.Raw.Span);
					if (!Fact || !P.Expr.IsValid()) { break; }
					if (P.AssignOp == EDnaTokenKind::Assign)
					{
						CheckAssignment(*Fact, P.Expr, Line.Raw.Span);
					}
					else
					{
						if (Fact->Type != EDnaFactType::Int && Fact->Type != EDnaFactType::Float)
						{
							Fail(TEXT("DNS4001"), Line.Raw.Span,
								FText::Format(LOCTEXT("CompoundOnNonNumber", "'{0}' is a {1} fact; '+=' and '-=' only apply to int and float facts"),
									FText::FromString(Fact->Name), FText::FromString(::DreamNarrative::LexToString(Fact->Type))));
						}
						const FDnaTypeInfo Value = CheckExpr(*P.Expr);
						if (!Value.IsUnknown() && !Value.IsNumeric())
						{
							Fail(TEXT("DNS4001"), P.Expr->Span,
								FText::Format(LOCTEXT("CompoundValue", "Expected a number here, found {0}"), FText::FromString(Value.Describe())));
						}
					}
					break;
				}
				case EDnaCommandKind::Accept:
				case EDnaCommandKind::Complete:
				case EDnaCommandKind::Fail:
					RequireQuest(P.Name, Line.Raw.Span);
					break;
				case EDnaCommandKind::Advance:
					RequireChapter(P.Name, Line.Raw.Span);
					break;
				case EDnaCommandKind::Play:
					RequireStage(P.Name, Line.Raw.Span);
					break;
				default:
					break;
				}
			}

			// ---------------------------------------------------------------- references

			const FDnaFactInfo* RequireFact(const FString& Name, const FDnaSpan& Span)
			{
				if (const FDnaFactInfo* Fact = Program.FindFact(Name))
				{
					return Fact;
				}
				Fail(TEXT("DNS3001"), Span,
					FText::Format(LOCTEXT("UnknownFact", "No fact called '{0}' is declared"), FText::FromString(Name)));
				return nullptr;
			}

			void RequireCharacter(const FString& Name, const FDnaSpan& Span, bool bIsSpeaker)
			{
				if (Program.IsCharacter(Name)) { return; }
				if (bIsSpeaker)
				{
					Fail(TEXT("DNS3002"), Span,
						FText::Format(LOCTEXT("UnknownSpeaker", "'{0}' is not a declared character. Declare it with `character {0}`, or start the line with `> ` if it is narration"),
							FText::FromString(Name)));
				}
				else
				{
					Fail(TEXT("DNS3002"), Span,
						FText::Format(LOCTEXT("UnknownCharacter", "'{0}' is not a declared character"), FText::FromString(Name)));
				}
			}

			void RequireDialogue(const FString& Name, const FDnaSpan& Span)
			{
				if (!Program.IsDialogue(Name))
				{
					Fail(TEXT("DNS3003"), Span,
						FText::Format(LOCTEXT("UnknownDialogue", "No dialogue called '{0}' exists"), FText::FromString(Name)));
				}
			}

			void RequireQuest(const FString& Name, const FDnaSpan& Span)
			{
				if (!Program.IsQuest(Name))
				{
					Fail(TEXT("DNS3005"), Span,
						FText::Format(LOCTEXT("UnknownQuest", "No quest called '{0}' exists"), FText::FromString(Name)));
				}
			}

			void RequireStage(const FString& Name, const FDnaSpan& Span)
			{
				if (!Program.IsStage(Name))
				{
					Fail(TEXT("DNS3006"), Span,
						FText::Format(LOCTEXT("UnknownStage", "No stage called '{0}' exists"), FText::FromString(Name)));
				}
			}

			void RequireChapter(const FString& Name, const FDnaSpan& Span)
			{
				if (!Program.IsChapter(Name))
				{
					Fail(TEXT("DNS3009"), Span,
						FText::Format(LOCTEXT("UnknownChapter", "No chapter called '{0}' exists"), FText::FromString(Name)));
				}
			}

			// ---------------------------------------------------------------- types

			/** A condition may be bool or numeric (non-zero is true). */
			void CheckCondition(const FDnaExprPtr& Expr)
			{
				const FDnaTypeInfo Type = CheckExpr(*Expr);
				if (!Type.IsUnknown() && Type.Kind != EKind::Bool && !Type.IsNumeric())
				{
					Fail(TEXT("DNS4001"), Expr->Span,
						FText::Format(LOCTEXT("ConditionType", "A condition must be true/false or a number, not a {0}"), FText::FromString(Type.Describe())));
				}
			}

			/** `fact = expr`: the value must fit the fact's type, set and range. */
			void CheckAssignment(const FDnaFactInfo& Fact, const FDnaExprPtr& Expr, const FDnaSpan& Span)
			{
				if (!Expr.IsValid()) { return; }
				switch (Fact.Type)
				{
				case EDnaFactType::Tag:
				{
					const FDnaTypeInfo Value = CheckTagOperand(*Expr, Fact);
					if (!Value.IsUnknown() && Value.Kind != EKind::Tag)
					{
						Fail(TEXT("DNS4001"), Expr->Span,
							FText::Format(LOCTEXT("AssignTag", "'{0}' holds one of ({1}); a {2} cannot be assigned to it"),
								FText::FromString(Fact.Name), FText::FromString(FString::Join(Fact.Domain, TEXT(", "))), FText::FromString(Value.Describe())));
					}
					break;
				}
				case EDnaFactType::Bool:
				{
					const FDnaTypeInfo Value = CheckExpr(*Expr);
					if (!Value.IsUnknown() && Value.Kind != EKind::Bool)
					{
						Fail(TEXT("DNS4001"), Expr->Span,
							FText::Format(LOCTEXT("AssignBool", "'{0}' is a bool fact; a {1} cannot be assigned to it"),
								FText::FromString(Fact.Name), FText::FromString(Value.Describe())));
					}
					break;
				}
				case EDnaFactType::Int:
				case EDnaFactType::Float:
				{
					const FDnaTypeInfo Value = CheckExpr(*Expr);
					if (!Value.IsUnknown() && !Value.IsNumeric())
					{
						Fail(TEXT("DNS4001"), Expr->Span,
							FText::Format(LOCTEXT("AssignNumber", "'{0}' is a {1} fact; a {2} cannot be assigned to it"),
								FText::FromString(Fact.Name), FText::FromString(::DreamNarrative::LexToString(Fact.Type)), FText::FromString(Value.Describe())));
					}
					else if (Fact.bHasRange && Expr->Kind == EDnaExprKind::Literal)
					{
						const double Number = Expr->Literal.AsNumber();
						if (Number < Fact.RangeMin || Number > Fact.RangeMax)
						{
							Fail(TEXT("DNS4003"), Expr->Span,
								FText::Format(LOCTEXT("OutOfRange", "{0} is outside the declared range {1}..{2} of '{3}'"),
									FText::FromString(Expr->Literal.ToString()), Fact.RangeMin, Fact.RangeMax, FText::FromString(Fact.Name)));
						}
					}
					break;
				}
				}
			}

			/** An operand that may be a bare member of Fact's set. */
			FDnaTypeInfo CheckTagOperand(const FDnaExpr& Expr, const FDnaFactInfo& Fact)
			{
				if (Expr.Kind == EDnaExprKind::Name && Expr.Path.Num() == 1 && !Program.FindFact(Expr.Name) && !Program.IsQuest(Expr.Path[0]))
				{
					if (!Fact.HasMember(Expr.Name))
					{
						Fail(TEXT("DNS4002"), Expr.Span,
							FText::Format(LOCTEXT("NotAMember", "'{0}' is not one of the values of '{1}' ({2})"),
								FText::FromString(Expr.Name), FText::FromString(Fact.Name), FText::FromString(FString::Join(Fact.Domain, TEXT(", ")))));
						return FDnaTypeInfo::Of(EKind::Unknown);
					}
					FDnaTypeInfo Type = FDnaTypeInfo::Of(EKind::Tag);
					Type.TagFact = &Fact;
					return Type;
				}
				return CheckExpr(Expr);
			}

			/** An operand compared with the current chapter must be a chapter name. */
			FDnaTypeInfo CheckChapterOperand(const FDnaExpr& Expr)
			{
				if (Expr.Kind == EDnaExprKind::Name && Expr.Path.Num() == 1 && !Program.FindFact(Expr.Name))
				{
					RequireChapter(Expr.Name, Expr.Span);
					return FDnaTypeInfo::Of(EKind::Chapter);
				}
				return CheckExpr(Expr);
			}

			FDnaTypeInfo CheckExpr(const FDnaExpr& Expr)
			{
				switch (Expr.Kind)
				{
				case EDnaExprKind::Literal:
					switch (Expr.Literal.Type)
					{
					case EDnaFactType::Bool: return FDnaTypeInfo::Of(EKind::Bool);
					case EDnaFactType::Int: return FDnaTypeInfo::Of(EKind::Int);
					case EDnaFactType::Float: return FDnaTypeInfo::Of(EKind::Float);
					default: return FDnaTypeInfo::Of(EKind::Unknown);
					}
				case EDnaExprKind::Chapter:
					return FDnaTypeInfo::Of(EKind::Chapter);
				case EDnaExprKind::Name:
					return CheckName(Expr);
				case EDnaExprKind::Call:
					return CheckCall(Expr);
				case EDnaExprKind::Unary:
				{
					const FDnaTypeInfo Operand = CheckExpr(*Expr.Args[0]);
					if (Expr.Op == EDnaExprOp::Not)
					{
						if (!Operand.IsUnknown() && Operand.Kind != EKind::Bool && !Operand.IsNumeric())
						{
							Fail(TEXT("DNS4001"), Expr.Span,
								FText::Format(LOCTEXT("NotOperand", "'not' needs true/false or a number, not a {0}"), FText::FromString(Operand.Describe())));
						}
						return FDnaTypeInfo::Of(EKind::Bool);
					}
					if (!Operand.IsUnknown() && !Operand.IsNumeric())
					{
						Fail(TEXT("DNS4001"), Expr.Span,
							FText::Format(LOCTEXT("NegOperand", "'-' needs a number, not a {0}"), FText::FromString(Operand.Describe())));
						return FDnaTypeInfo::Of(EKind::Unknown);
					}
					return Operand;
				}
				case EDnaExprKind::Binary:
					return CheckBinary(Expr);
				case EDnaExprKind::In:
				{
					const FDnaTypeInfo Subject = CheckExpr(*Expr.Args[0]);
					if (Subject.Kind == EKind::Tag && Subject.TagFact)
					{
						for (int32 Index = 1; Index < Expr.Args.Num(); ++Index)
						{
							CheckTagOperand(*Expr.Args[Index], *Subject.TagFact);
						}
					}
					else if (Subject.Kind == EKind::Chapter)
					{
						for (int32 Index = 1; Index < Expr.Args.Num(); ++Index)
						{
							CheckChapterOperand(*Expr.Args[Index]);
						}
					}
					else if (!Subject.IsUnknown())
					{
						Fail(TEXT("DNS4001"), Expr.Span,
							FText::Format(LOCTEXT("InSubject", "'in' applies to a tag fact or the chapter, not a {0}"), FText::FromString(Subject.Describe())));
					}
					return FDnaTypeInfo::Of(EKind::Bool);
				}
				}
				return FDnaTypeInfo::Of(EKind::Unknown);
			}

			FDnaTypeInfo CheckName(const FDnaExpr& Expr)
			{
				// quest state
				if (Expr.Path.Num() == 2 && Program.IsQuest(Expr.Path[0]))
				{
					const FString& Property = Expr.Path[1];
					if (Property == TEXT("available") || Property == TEXT("active") || Property == TEXT("completed") || Property == TEXT("failed"))
					{
						return FDnaTypeInfo::Of(EKind::Bool);
					}
					Fail(TEXT("DNS4005"), Expr.Span,
						FText::Format(LOCTEXT("QuestProperty", "'{0}' is not a quest state; use available, active, completed or failed"), FText::FromString(Property)));
					return FDnaTypeInfo::Of(EKind::Unknown);
				}
				if (const FDnaFactInfo* Fact = Program.FindFact(Expr.Name))
				{
					return FDnaTypeInfo::OfFact(*Fact);
				}
				Fail(TEXT("DNS3001"), Expr.Span,
					FText::Format(LOCTEXT("UnknownName", "No fact called '{0}' is declared"), FText::FromString(Expr.Name)));
				return FDnaTypeInfo::Of(EKind::Unknown);
			}

			FDnaTypeInfo CheckCall(const FDnaExpr& Expr)
			{
				if (Expr.Name == TEXT("visited"))
				{
					if (Expr.Args.Num() != 1 || Expr.Args[0]->Kind != EDnaExprKind::Name)
					{
						Fail(TEXT("DNS4005"), Expr.Span, LOCTEXT("VisitedArgs", "'visited' takes exactly one dialogue name"));
					}
					else
					{
						RequireDialogue(Expr.Args[0]->Name, Expr.Args[0]->Span);
					}
					return FDnaTypeInfo::Of(EKind::Int);
				}
				if (Expr.Name == TEXT("random"))
				{
					if (Expr.Args.Num() != 2)
					{
						Fail(TEXT("DNS4005"), Expr.Span, LOCTEXT("RandomArgs", "'random' takes two numbers: random(min, max)"));
						return FDnaTypeInfo::Of(EKind::Int);
					}
					bool bFloat = false;
					for (const FDnaExprPtr& Arg : Expr.Args)
					{
						const FDnaTypeInfo Type = CheckExpr(*Arg);
						if (!Type.IsUnknown() && !Type.IsNumeric())
						{
							Fail(TEXT("DNS4005"), Arg->Span, LOCTEXT("RandomArgType", "'random' arguments must be numbers"));
						}
						bFloat |= (Type.Kind == EKind::Float);
					}
					return FDnaTypeInfo::Of(bFloat ? EKind::Float : EKind::Int);
				}
				if (Expr.Name == TEXT("chance"))
				{
					if (Expr.Args.Num() != 1)
					{
						Fail(TEXT("DNS4005"), Expr.Span, LOCTEXT("ChanceArgs", "'chance' takes one probability: chance(30%)"));
					}
					else
					{
						const FDnaTypeInfo Type = CheckExpr(*Expr.Args[0]);
						if (!Type.IsUnknown() && !Type.IsNumeric())
						{
							Fail(TEXT("DNS4005"), Expr.Args[0]->Span, LOCTEXT("ChanceArgType", "'chance' needs a number or a percentage"));
						}
					}
					return FDnaTypeInfo::Of(EKind::Bool);
				}
				Fail(TEXT("DNS4005"), Expr.Span,
					FText::Format(LOCTEXT("UnknownFunction", "'{0}' is not a built-in; the built-ins are visited, random and chance"), FText::FromString(Expr.Name)));
				for (const FDnaExprPtr& Arg : Expr.Args) { CheckExpr(*Arg); }
				return FDnaTypeInfo::Of(EKind::Unknown);
			}

			FDnaTypeInfo CheckBinary(const FDnaExpr& Expr)
			{
				const FDnaExpr& Left = *Expr.Args[0];
				const FDnaExpr& Right = *Expr.Args[1];

				switch (Expr.Op)
				{
				case EDnaExprOp::And:
				case EDnaExprOp::Or:
				{
					for (const FDnaExpr* Side : { &Left, &Right })
					{
						const FDnaTypeInfo Type = CheckExpr(*Side);
						if (!Type.IsUnknown() && Type.Kind != EKind::Bool && !Type.IsNumeric())
						{
							Fail(TEXT("DNS4001"), Side->Span,
								FText::Format(LOCTEXT("LogicOperand", "'{0}' needs true/false or numbers, not a {1}"),
									FText::FromString(LexToString(Expr.Op)), FText::FromString(Type.Describe())));
						}
					}
					return FDnaTypeInfo::Of(EKind::Bool);
				}
				case EDnaExprOp::Eq:
				case EDnaExprOp::Ne:
				{
					// Tag and chapter comparisons resolve the other side as a member name.
					if (IsTagFactRef(Left)) { CheckTagOperand(Right, *Program.FindFact(Left.Name)); CheckExpr(Left); return FDnaTypeInfo::Of(EKind::Bool); }
					if (IsTagFactRef(Right)) { CheckTagOperand(Left, *Program.FindFact(Right.Name)); CheckExpr(Right); return FDnaTypeInfo::Of(EKind::Bool); }
					if (Left.Kind == EDnaExprKind::Chapter) { CheckChapterOperand(Right); return FDnaTypeInfo::Of(EKind::Bool); }
					if (Right.Kind == EDnaExprKind::Chapter) { CheckChapterOperand(Left); return FDnaTypeInfo::Of(EKind::Bool); }

					const FDnaTypeInfo L = CheckExpr(Left);
					const FDnaTypeInfo R = CheckExpr(Right);
					if (!L.IsUnknown() && !R.IsUnknown())
					{
						const bool bCompatible = (L.IsNumeric() && R.IsNumeric()) || L.Kind == R.Kind;
						if (!bCompatible)
						{
							Fail(TEXT("DNS4001"), Expr.Span,
								FText::Format(LOCTEXT("EqMismatch", "Cannot compare a {0} with a {1}"),
									FText::FromString(L.Describe()), FText::FromString(R.Describe())));
						}
					}
					return FDnaTypeInfo::Of(EKind::Bool);
				}
				case EDnaExprOp::Lt:
				case EDnaExprOp::Le:
				case EDnaExprOp::Gt:
				case EDnaExprOp::Ge:
				{
					const FDnaTypeInfo L = CheckExpr(Left);
					const FDnaTypeInfo R = CheckExpr(Right);
					for (const FDnaTypeInfo* Side : { &L, &R })
					{
						if (!Side->IsUnknown() && !Side->IsNumeric())
						{
							Fail(TEXT("DNS4004"), Expr.Span,
								FText::Format(LOCTEXT("OrderMismatch", "'{0}' orders numbers; a {1} has no order"),
									FText::FromString(LexToString(Expr.Op)), FText::FromString(Side->Describe())));
							break;
						}
					}
					return FDnaTypeInfo::Of(EKind::Bool);
				}
				case EDnaExprOp::Add:
				case EDnaExprOp::Sub:
				case EDnaExprOp::Mul:
				case EDnaExprOp::Div:
				case EDnaExprOp::Mod:
				{
					const FDnaTypeInfo L = CheckExpr(Left);
					const FDnaTypeInfo R = CheckExpr(Right);
					bool bFloat = false;
					for (const FDnaTypeInfo* Side : { &L, &R })
					{
						if (Side->IsUnknown()) { continue; }
						if (!Side->IsNumeric())
						{
							Fail(TEXT("DNS4001"), Expr.Span,
								FText::Format(LOCTEXT("ArithMismatch", "'{0}' needs numbers, not a {1}"),
									FText::FromString(LexToString(Expr.Op)), FText::FromString(Side->Describe())));
							return FDnaTypeInfo::Of(EKind::Unknown);
						}
						bFloat |= (Side->Kind == EKind::Float);
					}
					if (Expr.Op == EDnaExprOp::Mod && bFloat)
					{
						Fail(TEXT("DNS4001"), Expr.Span, LOCTEXT("ModFloat", "'%' needs whole numbers on both sides"));
					}
					return FDnaTypeInfo::Of(bFloat ? EKind::Float : EKind::Int);
				}
				default:
					return FDnaTypeInfo::Of(EKind::Unknown);
				}
			}

			bool IsTagFactRef(const FDnaExpr& Expr) const
			{
				if (Expr.Kind != EDnaExprKind::Name) { return false; }
				const FDnaFactInfo* Fact = Program.FindFact(Expr.Name);
				return Fact && Fact->Type == EDnaFactType::Tag;
			}
		};
	}

	bool ValidateDnaProgram(const FDnaProgram& Program, FDnaDiagnosticSink& Diagnostics)
	{
		FValidator Validator(Program, Diagnostics);
		return Validator.Run();
	}

	bool ValidateDnaExpression(const FDnaProgram& Program, const FDnaExprPtr& Expr, bool bAsCondition, FDnaDiagnosticSink& Diagnostics)
	{
		FValidator Validator(Program, Diagnostics);
		return Validator.RunOnExpression(Expr, bAsCondition);
	}
}

#undef LOCTEXT_NAMESPACE
