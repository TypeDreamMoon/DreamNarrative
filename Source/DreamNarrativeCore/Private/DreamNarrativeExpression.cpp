// Copyright DreamMoon. All Rights Reserved.

#include "DreamNarrativeExpression.h"

bool FDnaCompiledExpr::IsConstant() const
{
	for (const FDnaExprNode& Node : Nodes)
	{
		switch (Node.Kind)
		{
		case EDnaExprNodeKind::Fact:
		case EDnaExprNodeKind::Chapter:
		case EDnaExprNodeKind::QuestState:
		case EDnaExprNodeKind::Visited:
		case EDnaExprNodeKind::Random:
		case EDnaExprNodeKind::Chance:
			return false;
		default:
			break;
		}
	}
	return IsValid();
}

namespace DreamNarrative
{
	namespace
	{
		/** A context that has no world; every world read yields a default and flags itself. */
		class FNoWorldContext : public IDnaEvalContext
		{
		public:
			mutable bool bTouchedWorld = false;
			virtual FDnaValue GetFactValue(FName) const override { bTouchedWorld = true; return FDnaValue(); }
			virtual FName GetCurrentChapter() const override { bTouchedWorld = true; return NAME_None; }
			virtual bool GetQuestState(FName, EDnaQuestStateQuery) const override { bTouchedWorld = true; return false; }
			virtual int32 GetVisitCount(FName) const override { bTouchedWorld = true; return 0; }
			virtual float NextRandomUnit() override { bTouchedWorld = true; return 0.f; }
		};

		FDnaValue Number(double Value, bool bInt)
		{
			return bInt ? FDnaValue::FromInt(static_cast<int32>(Value)) : FDnaValue::FromFloat(static_cast<float>(Value));
		}

		FDnaValue Eval(const FDnaCompiledExpr& Expr, int32 Index, IDnaEvalContext& Context, int32 Depth)
		{
			if (!Expr.Nodes.IsValidIndex(Index) || Depth > 256)
			{
				return FDnaValue();
			}
			const FDnaExprNode& Node = Expr.Nodes[Index];
			auto Child = [&](int32 Which) -> FDnaValue
			{
				return Node.Children.IsValidIndex(Which) ? Eval(Expr, Node.Children[Which], Context, Depth + 1) : FDnaValue();
			};

			switch (Node.Kind)
			{
			case EDnaExprNodeKind::Literal:
				return Node.Literal;
			case EDnaExprNodeKind::Fact:
				return Context.GetFactValue(Node.Name);
			case EDnaExprNodeKind::Member:
			case EDnaExprNodeKind::ChapterName:
				return FDnaValue::FromTag(Node.Name);
			case EDnaExprNodeKind::Chapter:
				return FDnaValue::FromTag(Context.GetCurrentChapter());
			case EDnaExprNodeKind::QuestState:
				return FDnaValue::FromBool(Context.GetQuestState(Node.Name, Node.QuestQuery));
			case EDnaExprNodeKind::Visited:
				return FDnaValue::FromInt(Context.GetVisitCount(Node.Name));
			case EDnaExprNodeKind::Random:
			{
				const FDnaValue Min = Child(0);
				const FDnaValue Max = Child(1);
				const float Unit = Context.NextRandomUnit();
				if (Min.Type == EDnaFactType::Int && Max.Type == EDnaFactType::Int)
				{
					const int32 Lo = FMath::Min(Min.Int, Max.Int);
					const int32 Hi = FMath::Max(Min.Int, Max.Int);
					return FDnaValue::FromInt(FMath::Clamp(Lo + FMath::FloorToInt(Unit * (Hi - Lo + 1)), Lo, Hi));
				}
				const double Lo = FMath::Min(Min.AsNumber(), Max.AsNumber());
				const double Hi = FMath::Max(Min.AsNumber(), Max.AsNumber());
				return FDnaValue::FromFloat(static_cast<float>(Lo + Unit * (Hi - Lo)));
			}
			case EDnaExprNodeKind::Chance:
			{
				const double Probability = Child(0).AsNumber();
				return FDnaValue::FromBool(Context.NextRandomUnit() < Probability);
			}
			case EDnaExprNodeKind::Unary:
			{
				const FDnaValue Operand = Child(0);
				if (Node.Operator == EDnaExprOperator::Not)
				{
					return FDnaValue::FromBool(!Operand.AsBool());
				}
				return Number(-Operand.AsNumber(), Operand.Type == EDnaFactType::Int);
			}
			case EDnaExprNodeKind::Binary:
			{
				// short-circuit logic
				if (Node.Operator == EDnaExprOperator::And)
				{
					return FDnaValue::FromBool(Child(0).AsBool() && Child(1).AsBool());
				}
				if (Node.Operator == EDnaExprOperator::Or)
				{
					return FDnaValue::FromBool(Child(0).AsBool() || Child(1).AsBool());
				}
				const FDnaValue Left = Child(0);
				const FDnaValue Right = Child(1);
				const bool bInt = Left.Type == EDnaFactType::Int && Right.Type == EDnaFactType::Int;
				const double L = Left.AsNumber();
				const double R = Right.AsNumber();
				switch (Node.Operator)
				{
				case EDnaExprOperator::Eq: return FDnaValue::FromBool(Left.Equals(Right));
				case EDnaExprOperator::Ne: return FDnaValue::FromBool(!Left.Equals(Right));
				case EDnaExprOperator::Lt: return FDnaValue::FromBool(L < R);
				case EDnaExprOperator::Le: return FDnaValue::FromBool(L <= R);
				case EDnaExprOperator::Gt: return FDnaValue::FromBool(L > R);
				case EDnaExprOperator::Ge: return FDnaValue::FromBool(L >= R);
				case EDnaExprOperator::Add: return Number(L + R, bInt);
				case EDnaExprOperator::Sub: return Number(L - R, bInt);
				case EDnaExprOperator::Mul: return Number(L * R, bInt);
				case EDnaExprOperator::Div: return R != 0.0 ? Number(bInt ? FMath::TruncToDouble(L / R) : L / R, bInt) : Number(0.0, bInt);
				// x % 0 is 0 by definition here; x % -1 is 0 in arithmetic, and INT_MIN % -1 traps on x86.
				case EDnaExprOperator::Mod: return FDnaValue::FromInt(Right.Int == 0 || Right.Int == -1 ? 0 : Left.Int % Right.Int);
				default: return FDnaValue();
				}
			}
			case EDnaExprNodeKind::In:
			{
				const FDnaValue Subject = Child(0);
				for (int32 Which = 1; Which < Node.Children.Num(); ++Which)
				{
					if (Subject.Equals(Child(Which)))
					{
						return FDnaValue::FromBool(true);
					}
				}
				return FDnaValue::FromBool(false);
			}
			}
			return FDnaValue();
		}
	}

	FDnaValue EvaluateExpr(const FDnaCompiledExpr& Expr, IDnaEvalContext& Context)
	{
		if (!Expr.IsValid())
		{
			return FDnaValue();
		}
		return Eval(Expr, Expr.Root, Context, 0);
	}

	bool EvaluateCondition(const FDnaCompiledExpr& Expr, IDnaEvalContext& Context)
	{
		return Expr.IsValid() && EvaluateExpr(Expr, Context).AsBool();
	}

	bool EvaluateConstant(const FDnaCompiledExpr& Expr, FDnaValue& OutValue)
	{
		if (!Expr.IsConstant())
		{
			return false;
		}
		FNoWorldContext Context;
		OutValue = EvaluateExpr(Expr, Context);
		return !Context.bTouchedWorld;
	}
}
