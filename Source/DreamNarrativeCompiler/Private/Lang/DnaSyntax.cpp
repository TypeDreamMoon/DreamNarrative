// Copyright DreamMoon. All Rights Reserved.

#include "Lang/DnaSyntax.h"
#include "Lang/DnaDump.h"

namespace UE::DreamNarrative::Lang
{
	const TCHAR* LexToString(EDnaLineKind Kind)
	{
		switch (Kind)
		{
		case EDnaLineKind::Blank: return TEXT("Blank");
		case EDnaLineKind::CommentOnly: return TEXT("Comment");
		case EDnaLineKind::File: return TEXT("File");
		case EDnaLineKind::Character: return TEXT("Character");
		case EDnaLineKind::Fact: return TEXT("Fact");
		case EDnaLineKind::FactGroup: return TEXT("FactGroup");
		case EDnaLineKind::Chapter: return TEXT("Chapter");
		case EDnaLineKind::Quest: return TEXT("Quest");
		case EDnaLineKind::Dialogue: return TEXT("Dialogue");
		case EDnaLineKind::Stage: return TEXT("Stage");
		case EDnaLineKind::Property: return TEXT("Property");
		case EDnaLineKind::Flag: return TEXT("Flag");
		case EDnaLineKind::Implied: return TEXT("Implied");
		case EDnaLineKind::Implication: return TEXT("Implication");
		case EDnaLineKind::Advance: return TEXT("Advance");
		case EDnaLineKind::Available: return TEXT("Available");
		case EDnaLineKind::FailIf: return TEXT("FailIf");
		case EDnaLineKind::Journal: return TEXT("Journal");
		case EDnaLineKind::Objective: return TEXT("Objective");
		case EDnaLineKind::On: return TEXT("On");
		case EDnaLineKind::Subquest: return TEXT("Subquest");
		case EDnaLineKind::Reward: return TEXT("Reward");
		case EDnaLineKind::If: return TEXT("If");
		case EDnaLineKind::ElseIf: return TEXT("ElseIf");
		case EDnaLineKind::Else: return TEXT("Else");
		case EDnaLineKind::Choice: return TEXT("Choice");
		case EDnaLineKind::Jump: return TEXT("Jump");
		case EDnaLineKind::Speech: return TEXT("Speech");
		case EDnaLineKind::Narration: return TEXT("Narration");
		case EDnaLineKind::Continuation: return TEXT("Continuation");
		case EDnaLineKind::Command: return TEXT("Command");
		case EDnaLineKind::Duration: return TEXT("Duration");
		case EDnaLineKind::Track: return TEXT("Track");
		case EDnaLineKind::Shots: return TEXT("Shots");
		case EDnaLineKind::Shot: return TEXT("Shot");
		case EDnaLineKind::Restore: return TEXT("Restore");
		case EDnaLineKind::RestoreEntry: return TEXT("RestoreEntry");
		case EDnaLineKind::StageEntry: return TEXT("StageEntry");
		case EDnaLineKind::Test: return TEXT("Test");
		case EDnaLineKind::Explore: return TEXT("Explore");
		case EDnaLineKind::TestStep: return TEXT("TestStep");
		case EDnaLineKind::Unknown: return TEXT("Unknown");
		}
		return TEXT("?");
	}

	const TCHAR* LexToString(EDnaTestStepKind Kind)
	{
		switch (Kind)
		{
		case EDnaTestStepKind::None: return TEXT("?");
		case EDnaTestStepKind::Seed: return TEXT("seed");
		case EDnaTestStepKind::Given: return TEXT("given");
		case EDnaTestStepKind::Talk: return TEXT("talk");
		case EDnaTestStepKind::Start: return TEXT("start");
		case EDnaTestStepKind::Expect: return TEXT("expect");
		case EDnaTestStepKind::Set: return TEXT("set");
		case EDnaTestStepKind::Wait: return TEXT("wait");
		case EDnaTestStepKind::Jump: return TEXT("jump");
		case EDnaTestStepKind::Save: return TEXT("save");
		case EDnaTestStepKind::ExpectLine: return TEXT("line");
		case EDnaTestStepKind::Absent: return TEXT("absent");
		case EDnaTestStepKind::Pick: return TEXT("pick");
		case EDnaTestStepKind::Rolls: return TEXT("rolls");
		case EDnaTestStepKind::Depth: return TEXT("depth");
		case EDnaTestStepKind::Always: return TEXT("always");
		case EDnaTestStepKind::Eventually: return TEXT("eventually");
		case EDnaTestStepKind::Softlock: return TEXT("softlock");
		case EDnaTestStepKind::GivenChapter: return TEXT("chapter");
		case EDnaTestStepKind::GivenVisited: return TEXT("visited");
		case EDnaTestStepKind::GivenOnce: return TEXT("once");
		}
		return TEXT("?");
	}

	bool FDnaLineNode::OpensBlock() const
	{
		switch (Kind)
		{
		case EDnaLineKind::File:
		case EDnaLineKind::Character:
		case EDnaLineKind::FactGroup:
		case EDnaLineKind::Chapter:
		case EDnaLineKind::Quest:
		case EDnaLineKind::Dialogue:
		case EDnaLineKind::Stage:
		case EDnaLineKind::Implied:
		case EDnaLineKind::Journal:
		case EDnaLineKind::Objective:
		case EDnaLineKind::On:
		case EDnaLineKind::If:
		case EDnaLineKind::ElseIf:
		case EDnaLineKind::Else:
		case EDnaLineKind::Choice:
		case EDnaLineKind::Track:
		case EDnaLineKind::Shots:
		case EDnaLineKind::Restore:
		case EDnaLineKind::Test:
		case EDnaLineKind::Explore:
			return true;
		case EDnaLineKind::TestStep:
			return Payload.TestStep == EDnaTestStepKind::Given || Payload.TestStep == EDnaTestStepKind::Talk
				|| Payload.TestStep == EDnaTestStepKind::Start;
		default:
			return false;
		}
	}

	void FDnaLineNode::GetContentChildren(TArray<FDnaLineNode*>& Out) const
	{
		for (const FDnaLineNodePtr& Child : Children)
		{
			if (Child.IsValid() && !Child->IsFloating())
			{
				Out.Add(Child.Get());
			}
		}
	}

	namespace
	{
		void VisitNode(FDnaLineNode& Node, TFunctionRef<void(FDnaLineNode&)> Visitor)
		{
			Visitor(Node);
			for (const FDnaLineNodePtr& Child : Node.Children)
			{
				if (Child.IsValid())
				{
					VisitNode(*Child, Visitor);
				}
			}
		}
	}

	void FDnaDocument::ForEachNode(TFunctionRef<void(FDnaLineNode&)> Visitor)
	{
		for (const FDnaLineNodePtr& Root : Roots)
		{
			if (Root.IsValid())
			{
				VisitNode(*Root, Visitor);
			}
		}
	}

	void FDnaDocument::ForEachNode(TFunctionRef<void(const FDnaLineNode&)> Visitor) const
	{
		for (const FDnaLineNodePtr& Root : Roots)
		{
			if (Root.IsValid())
			{
				VisitNode(*Root, [&Visitor](FDnaLineNode& Node) { Visitor(Node); });
			}
		}
	}

	void FDnaDocument::GetRootsOfKind(EDnaLineKind Kind, TArray<FDnaLineNode*>& Out) const
	{
		for (const FDnaLineNodePtr& Root : Roots)
		{
			if (Root.IsValid() && Root->Kind == Kind)
			{
				Out.Add(Root.Get());
			}
		}
	}

	// ---------------------------------------------------------------- dump

	namespace
	{
		FString ExprText(const FDnaExprPtr& Expr)
		{
			return Expr.IsValid() ? Expr->ToString() : TEXT("<null>");
		}

		FString TextDump(const FDnaText& Text)
		{
			FString Result = TEXT("\"") + Text.Raw + TEXT("\"");
			if (Text.HasInterpolation())
			{
				TArray<FString> Parts;
				for (const FDnaTextPart& Part : Text.Parts)
				{
					Parts.Add(Part.IsExpr() ? TEXT("{") + Part.Expr->ToString() + TEXT("}") : TEXT("'") + Part.Literal + TEXT("'"));
				}
				Result += TEXT(" parts=[") + FString::Join(Parts, TEXT(" ")) + TEXT("]");
			}
			return Result;
		}

		FString ModsDump(const FDnaModifiers& Mods)
		{
			FString Result;
			if (Mods.Condition.IsValid()) { Result += TEXT(" if=") + Mods.Condition->ToString(); }
			if (Mods.bOnce) { Result += TEXT(" once"); }
			if (Mods.bHasDuration) { Result += FString::Printf(TEXT(" for=%gs"), Mods.DurationSeconds); }
			if (!Mods.Stage.IsEmpty()) { Result += TEXT(" @") + Mods.Stage; }
			if (!Mods.Id.IsEmpty()) { Result += TEXT(" #") + Mods.Id; }
			return Result;
		}

		FString ArgsDump(const TArray<FDnaToken>& Args)
		{
			TArray<FString> Parts;
			for (const FDnaToken& Token : Args)
			{
				if (Token.Kind == EDnaTokenKind::EndOfInput) { continue; }
				if (Token.Kind == EDnaTokenKind::String) { Parts.Add(TEXT("\"") + Token.Text + TEXT("\"")); }
				else if (Token.Kind == EDnaTokenKind::Duration) { Parts.Add(FString::Printf(TEXT("%gs"), Token.Real)); }
				else { Parts.Add(Token.Text); }
			}
			return FString::Join(Parts, TEXT(" "));
		}

		FString FactDump(const FDnaLinePayload& P)
		{
			FString Result = P.Name + TEXT(": ") + ::DreamNarrative::LexToString(P.FactType);
			if (P.bExtern) { Result += TEXT(" extern"); }
			if (P.bSession) { Result += TEXT(" session"); }
			if (P.Authority == EDnaAuthority::Server) { Result += TEXT(" server"); }
			if (P.Authority == EDnaAuthority::Client) { Result += TEXT(" client"); }
			if (P.bHasRange) { Result += FString::Printf(TEXT(" range %g..%g"), P.RangeMin, P.RangeMax); }
			if (P.Domain.Num() > 0) { Result += TEXT(" in (") + FString::Join(P.Domain, TEXT(", ")) + TEXT(")"); }
			if (P.DefaultExpr.IsValid()) { Result += TEXT(" = ") + P.DefaultExpr->ToString(); }
			if (!P.WasName.IsEmpty()) { Result += TEXT(" was ") + P.WasName; }
			return Result;
		}

		void DumpNode(const FDnaLineNode& Node, int32 Depth, TArray<FString>& Lines)
		{
			if (Node.IsFloating())
			{
				for (const FDnaLineNodePtr& Child : Node.Children) { if (Child.IsValid()) { DumpNode(*Child, Depth, Lines); } }
				return;
			}
			const FDnaLinePayload& P = Node.Payload;
			FString Detail;
			switch (Node.Kind)
			{
			case EDnaLineKind::Character:
				Detail = P.Name + (P.bPlayer ? TEXT(" player") : TEXT("")) + (P.WasName.IsEmpty() ? TEXT("") : TEXT(" was ") + P.WasName);
				break;
			case EDnaLineKind::Fact:
				Detail = FactDump(P);
				break;
			case EDnaLineKind::FactGroup:
				Detail = P.Name + (P.bExtern ? TEXT(" extern") : TEXT("")) + (P.bSession ? TEXT(" session") : TEXT(""));
				break;
			case EDnaLineKind::Chapter:
				Detail = P.Name + (P.Target.IsEmpty() ? TEXT("") : TEXT(" after ") + P.Target) + (P.WasName.IsEmpty() ? TEXT("") : TEXT(" was ") + P.WasName);
				break;
			case EDnaLineKind::Quest:
			case EDnaLineKind::Stage:
				Detail = P.Name + (P.WasName.IsEmpty() ? TEXT("") : TEXT(" was ") + P.WasName);
				break;
			case EDnaLineKind::Dialogue:
				Detail = P.Name;
				if (!P.ForCharacter.IsEmpty()) { Detail += TEXT(" for ") + P.ForCharacter; }
				if (P.Condition.IsValid()) { Detail += TEXT(" if ") + P.Condition->ToString(); }
				if (P.Priority != INDEX_NONE) { Detail += FString::Printf(TEXT(" priority %d"), P.Priority); }
				if (!P.WasName.IsEmpty()) { Detail += TEXT(" was ") + P.WasName; }
				break;
			case EDnaLineKind::Property:
				Detail = P.Name + TEXT(" = ") + TextDump(P.Text);
				break;
			case EDnaLineKind::Flag:
				Detail = P.Name;
				break;
			case EDnaLineKind::Implication:
				Detail = P.Name + TEXT(" = ") + ExprText(P.Expr);
				break;
			case EDnaLineKind::Advance:
				Detail = TEXT("-> ") + P.Target + (P.Condition.IsValid() ? TEXT(" if ") + P.Condition->ToString() : TEXT(""));
				break;
			case EDnaLineKind::Available:
			case EDnaLineKind::FailIf:
			case EDnaLineKind::If:
			case EDnaLineKind::ElseIf:
				Detail = ExprText(P.Condition);
				break;
			case EDnaLineKind::Objective:
				Detail = ExprText(P.Condition) + (P.bOptional ? TEXT(" optional") : TEXT(""));
				break;
			case EDnaLineKind::On:
				Detail = P.Name;
				break;
			case EDnaLineKind::Subquest:
				Detail = P.Name + (P.bAuto ? TEXT(" auto") : TEXT(""));
				break;
			case EDnaLineKind::Reward:
				Detail = P.RewardCategory + (P.RewardItem.IsEmpty() ? TEXT("") : TEXT(" ") + P.RewardItem) + FString::Printf(TEXT(" x%d"), P.RewardAmount);
				break;
			case EDnaLineKind::Choice:
				Detail = TextDump(P.Text) + ModsDump(P.Modifiers);
				break;
			case EDnaLineKind::Jump:
				Detail = P.bEnd ? TEXT("end") : P.Target;
				break;
			case EDnaLineKind::Speech:
				Detail = P.Name;
				if (P.SpeakerTags.Num() > 0) { Detail += TEXT("(") + FString::Join(P.SpeakerTags, TEXT(", ")) + TEXT(")"); }
				Detail += TEXT(" ") + TextDump(P.Text) + ModsDump(P.Modifiers);
				break;
			case EDnaLineKind::Narration:
				Detail = TextDump(P.Text) + ModsDump(P.Modifiers);
				break;
			case EDnaLineKind::Continuation:
				Detail = TextDump(P.Text);
				break;
			case EDnaLineKind::Command:
				switch (P.Command)
				{
				case EDnaCommandKind::Set:
					Detail = TEXT("set ") + P.Name + TEXT(" ") + LexToString(P.AssignOp) + TEXT(" ") + ExprText(P.Expr);
					break;
				case EDnaCommandKind::Accept: Detail = TEXT("accept ") + P.Name; break;
				case EDnaCommandKind::Complete: Detail = TEXT("complete ") + P.Name; break;
				case EDnaCommandKind::Fail: Detail = TEXT("fail ") + P.Name; break;
				case EDnaCommandKind::Advance: Detail = TEXT("advance ") + P.Name; break;
				case EDnaCommandKind::Play: Detail = TEXT("play ") + P.Name + (P.bAsync ? TEXT(" async") : TEXT("")); break;
				case EDnaCommandKind::Emit: Detail = TEXT("emit ") + P.Name; break;
				case EDnaCommandKind::Wait: Detail = FString::Printf(TEXT("wait %gs"), P.Seconds); break;
				default: Detail = TEXT("?"); break;
				}
				break;
			case EDnaLineKind::Duration:
				Detail = FString::Printf(TEXT("%gs"), P.Seconds);
				break;
			case EDnaLineKind::Track:
				Detail = P.Name;
				break;
			case EDnaLineKind::Shot:
			case EDnaLineKind::RestoreEntry:
				Detail = P.Name + TEXT(" ") + ArgsDump(P.Args);
				break;
			case EDnaLineKind::StageEntry:
				Detail = P.bIsRange
					? FString::Printf(TEXT("%g%s..%g%s "), P.TimeStart, P.bPercent ? TEXT("%") : TEXT("s"), P.TimeEnd, P.bPercent ? TEXT("%") : TEXT("s"))
					: FString::Printf(TEXT("%g%s "), P.TimeStart, P.bPercent ? TEXT("%") : TEXT("s"));
				Detail += P.Name + TEXT(" ") + ArgsDump(P.Args);
				break;
			case EDnaLineKind::Test:
			case EDnaLineKind::Explore:
				Detail = P.Name;
				break;
			case EDnaLineKind::TestStep:
				Detail = LexToString(P.TestStep);
				switch (P.TestStep)
				{
				case EDnaTestStepKind::Seed: Detail += FString::Printf(TEXT(" %d"), P.Seed); break;
				case EDnaTestStepKind::Talk:
				case EDnaTestStepKind::Start: Detail += TEXT(" ") + P.Name + (P.bStrict ? TEXT(" strict") : TEXT("")); break;
				case EDnaTestStepKind::Expect: Detail += TEXT(" ") + ExprText(P.Condition); break;
				case EDnaTestStepKind::Set: Detail += TEXT(" ") + P.Name + TEXT(" ") + LexToString(P.AssignOp) + TEXT(" ") + ExprText(P.Expr); break;
				case EDnaTestStepKind::Wait: Detail += FString::Printf(TEXT(" %gs"), P.Seconds); break;
				case EDnaTestStepKind::Jump: Detail += TEXT(" ") + P.Target; break;
				case EDnaTestStepKind::ExpectLine:
				case EDnaTestStepKind::Absent:
					if (P.bChoice) { Detail += TEXT(" -"); }
					if (!P.Target.IsEmpty()) { Detail += TEXT(" #") + P.Target; }
					else if (P.bChoice) { Detail += TEXT(" ") + TextDump(P.Text); }
					else if (P.Name.IsEmpty()) { Detail += TEXT(" > ") + TextDump(P.Text); }
					else
					{
						Detail += TEXT(" ") + P.Name;
						if (P.SpeakerTags.Num() > 0) { Detail += TEXT("(") + FString::Join(P.SpeakerTags, TEXT(", ")) + TEXT(")"); }
						Detail += TEXT(" ") + TextDump(P.Text);
					}
					break;
				case EDnaTestStepKind::Pick: Detail += P.Target.IsEmpty() ? TEXT(" ") + TextDump(P.Text) : TEXT(" #") + P.Target; break;
				case EDnaTestStepKind::Rolls:
					for (const float Roll : P.Rolls) { Detail += FString::Printf(TEXT(" %.9g"), Roll); }
					break;
				case EDnaTestStepKind::Depth: Detail += FString::Printf(TEXT(" %d"), P.Depth); break;
				case EDnaTestStepKind::GivenChapter: Detail += TEXT(" = ") + P.Target; break;
				case EDnaTestStepKind::GivenVisited: Detail += FString::Printf(TEXT("(%s) = %d"), *P.Target, P.Count); break;
				case EDnaTestStepKind::GivenOnce: Detail += TEXT(" #") + P.Target; break;
				case EDnaTestStepKind::Always:
				case EDnaTestStepKind::Eventually: Detail += TEXT(" ") + ExprText(P.Condition); break;
				case EDnaTestStepKind::Softlock:
					for (int32 Index = 0; Index < P.Goals.Num(); ++Index) { Detail += (Index == 0 ? TEXT(" ") : TEXT(", ")) + ExprText(P.Goals[Index]); }
					break;
				default: break;
				}
				break;
			default:
				break;
			}

			FString Line = FString::Printf(TEXT("%d: %s%s"), Node.LineNumber(), *FString::ChrN(Depth * 2, TEXT(' ')), LexToString(Node.Kind));
			if (!Detail.IsEmpty())
			{
				Line += TEXT(" ") + Detail.TrimEnd();
			}
			Lines.Add(Line);

			for (const FDnaLineNodePtr& Child : Node.Children)
			{
				if (Child.IsValid())
				{
					DumpNode(*Child, Depth + 1, Lines);
				}
			}
		}
	}

	FString DumpDnaTree(const FDnaDocument& Document)
	{
		TArray<FString> Lines;
		for (const FDnaLineNodePtr& Root : Document.Roots)
		{
			if (Root.IsValid())
			{
				DumpNode(*Root, 0, Lines);
			}
		}
		return FString::Join(Lines, TEXT("\n")) + TEXT("\n");
	}
}
