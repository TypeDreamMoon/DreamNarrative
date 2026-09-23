// Copyright DreamMoon. All Rights Reserved.

#include "Spec/DnaCoverage.h"
#include "DreamNarrativeExpression.h"

namespace UE::DreamNarrative::Tests
{
	namespace
	{
		const TCHAR* TypeName(EDnaFactType Type)
		{
			switch (Type)
			{
			case EDnaFactType::Bool: return TEXT("Bool");
			case EDnaFactType::Int: return TEXT("Int");
			case EDnaFactType::Float: return TEXT("Float");
			case EDnaFactType::Tag: return TEXT("Tag");
			}
			return TEXT("?");
		}

		/** Operand types in a fixed order, so `1.5 + x` and `x + 1.5` are the same cell. */
		FString TypePair(const FDnaValue& A, const FDnaValue& B)
		{
			const bool bSwap = static_cast<uint8>(B.Type) < static_cast<uint8>(A.Type);
			return FString::Printf(TEXT("%s+%s"), TypeName(bSwap ? B.Type : A.Type), TypeName(bSwap ? A.Type : B.Type));
		}

		const TCHAR* OperatorName(EDnaExprOperator Operator)
		{
			switch (Operator)
			{
			case EDnaExprOperator::Or: return TEXT("Or");
			case EDnaExprOperator::And: return TEXT("And");
			case EDnaExprOperator::Not: return TEXT("Not");
			case EDnaExprOperator::Eq: return TEXT("Eq");
			case EDnaExprOperator::Ne: return TEXT("Ne");
			case EDnaExprOperator::Lt: return TEXT("Lt");
			case EDnaExprOperator::Le: return TEXT("Le");
			case EDnaExprOperator::Gt: return TEXT("Gt");
			case EDnaExprOperator::Ge: return TEXT("Ge");
			case EDnaExprOperator::Add: return TEXT("Add");
			case EDnaExprOperator::Sub: return TEXT("Sub");
			case EDnaExprOperator::Mul: return TEXT("Mul");
			case EDnaExprOperator::Div: return TEXT("Div");
			case EDnaExprOperator::Mod: return TEXT("Mod");
			case EDnaExprOperator::Neg: return TEXT("Neg");
			default: return TEXT("?");
			}
		}
	}

	void FDnaCoverage::OnExpression(const FDnaExprNode& Node, const FDnaValue* Left, const FDnaValue* Right, const FDnaValue& Result)
	{
		switch (Node.Kind)
		{
		case EDnaExprNodeKind::Literal: Hit(FString(TEXT("expr.Literal.")) + TypeName(Result.Type)); break;
		case EDnaExprNodeKind::Fact: Hit(FString(TEXT("expr.Fact.")) + TypeName(Result.Type)); break;
		case EDnaExprNodeKind::Member: Hit(TEXT("expr.Member")); break;
		case EDnaExprNodeKind::Chapter: Hit(TEXT("expr.Chapter")); break;
		case EDnaExprNodeKind::ChapterName: Hit(TEXT("expr.ChapterName")); break;
		case EDnaExprNodeKind::QuestState: Hit(TEXT("expr.QuestState")); break;
		case EDnaExprNodeKind::Visited: Hit(TEXT("expr.Visited")); break;
		case EDnaExprNodeKind::Random: Hit(FString(TEXT("expr.Random.")) + TypeName(Result.Type)); break;
		case EDnaExprNodeKind::Chance: Hit(TEXT("expr.Chance")); break;
		case EDnaExprNodeKind::Unary:
			if (Node.Operator == EDnaExprOperator::Not)
			{
				Hit(TEXT("expr.Not"));
			}
			else if (Left)
			{
				Hit(FString(TEXT("expr.Neg.")) + TypeName(Left->Type));
			}
			break;
		case EDnaExprNodeKind::Binary:
			if (Node.Operator == EDnaExprOperator::And || Node.Operator == EDnaExprOperator::Or)
			{
				Hit(FString(TEXT("expr.")) + OperatorName(Node.Operator) + (Right ? TEXT("") : TEXT(".short")));
			}
			else if (Left && Right)
			{
				Hit(FString::Printf(TEXT("expr.%s.%s"), OperatorName(Node.Operator), *TypePair(*Left, *Right)));
				if ((Node.Operator == EDnaExprOperator::Div || Node.Operator == EDnaExprOperator::Mod) && Right->AsNumber() == 0.0)
				{
					Hit(FString::Printf(TEXT("expr.%s.zero"), OperatorName(Node.Operator)));
				}
			}
			break;
		case EDnaExprNodeKind::In:
			if (Left)
			{
				Hit(FString(TEXT("expr.In.")) + TypeName(Left->Type));
			}
			break;
		}
	}

	void FDnaCoverage::OnFactWrite(FName Fact, EDnaFactType Type, EDnaFactOwner Owner, EDnaWriteSource Source, EDnaFactWriteResult Result)
	{
		Hit(FString(TEXT("ledger.write.")) + LexToString(Result));
		const bool bAccepted = Result == EDnaFactWriteResult::Written || Result == EDnaFactWriteResult::Unchanged || Result == EDnaFactWriteResult::Clamped;
		if (Result == EDnaFactWriteResult::Written)
		{
			Hit(FString(TEXT("ledger.write.Written.")) + TypeName(Type));
		}
		if (bAccepted && Source == EDnaWriteSource::Game)
		{
			Hit(TEXT("ledger.write.by-game"));
		}
		if (bAccepted && Source == EDnaWriteSource::Narrative && Owner == EDnaFactOwner::Extern)
		{
			Hit(TEXT("ledger.write.narrative-to-extern"));
		}
	}
}
