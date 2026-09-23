// Copyright DreamMoon. All Rights Reserved.

#include "DreamNarrativeExpression.h"
#include "DreamNarrativeTrace.h"

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
			IDnaTrace* Trace = Context.GetTrace();
			auto Report = [&Node, Trace](const FDnaValue* Left, const FDnaValue* Right, const FDnaValue& Result) -> FDnaValue
			{
				if (Trace)
				{
					Trace->OnExpression(Node, Left, Right, Result);
				}
				return Result;
			};

			switch (Node.Kind)
			{
			case EDnaExprNodeKind::Literal:
				return Report(nullptr, nullptr, Node.Literal);
			case EDnaExprNodeKind::Fact:
				return Report(nullptr, nullptr, Context.GetFactValue(Node.Name));
			case EDnaExprNodeKind::Member:
			case EDnaExprNodeKind::ChapterName:
				return Report(nullptr, nullptr, FDnaValue::FromTag(Node.Name));
			case EDnaExprNodeKind::Chapter:
				return Report(nullptr, nullptr, FDnaValue::FromTag(Context.GetCurrentChapter()));
			case EDnaExprNodeKind::QuestState:
				return Report(nullptr, nullptr, FDnaValue::FromBool(Context.GetQuestState(Node.Name, Node.QuestQuery)));
			case EDnaExprNodeKind::Visited:
				return Report(nullptr, nullptr, FDnaValue::FromInt(Context.GetVisitCount(Node.Name)));
			case EDnaExprNodeKind::Random:
			{
				const FDnaValue Min = Child(0);
				const FDnaValue Max = Child(1);
				const float Unit = Context.NextRandomUnit();
				if (Min.Type == EDnaFactType::Int && Max.Type == EDnaFactType::Int)
				{
					const int32 Lo = FMath::Min(Min.Int, Max.Int);
					const int32 Hi = FMath::Max(Min.Int, Max.Int);
					return Report(&Min, &Max, FDnaValue::FromInt(FMath::Clamp(Lo + FMath::FloorToInt(Unit * (Hi - Lo + 1)), Lo, Hi)));
				}
				const double Lo = FMath::Min(Min.AsNumber(), Max.AsNumber());
				const double Hi = FMath::Max(Min.AsNumber(), Max.AsNumber());
				return Report(&Min, &Max, FDnaValue::FromFloat(static_cast<float>(Lo + Unit * (Hi - Lo))));
			}
			case EDnaExprNodeKind::Chance:
			{
				const FDnaValue Probability = Child(0);
				return Report(&Probability, nullptr, FDnaValue::FromBool(Context.NextRandomUnit() < Probability.AsNumber()));
			}
			case EDnaExprNodeKind::Unary:
			{
				const FDnaValue Operand = Child(0);
				if (Node.Operator == EDnaExprOperator::Not)
				{
					return Report(&Operand, nullptr, FDnaValue::FromBool(!Operand.AsBool()));
				}
				return Report(&Operand, nullptr, Number(-Operand.AsNumber(), Operand.Type == EDnaFactType::Int));
			}
			case EDnaExprNodeKind::Binary:
			{
				const FDnaValue Left = Child(0);
				// `and` / `or` short-circuit: the right side is not evaluated when the left decides.
				if (Node.Operator == EDnaExprOperator::And || Node.Operator == EDnaExprOperator::Or)
				{
					const bool bDecided = (Node.Operator == EDnaExprOperator::And) != Left.AsBool();
					if (bDecided)
					{
						return Report(&Left, nullptr, FDnaValue::FromBool(Left.AsBool()));
					}
					const FDnaValue Right = Child(1);
					return Report(&Left, &Right, FDnaValue::FromBool(Right.AsBool()));
				}
				const FDnaValue Right = Child(1);
				const bool bInt = Left.Type == EDnaFactType::Int && Right.Type == EDnaFactType::Int;
				const double L = Left.AsNumber();
				const double R = Right.AsNumber();
				FDnaValue Result;
				switch (Node.Operator)
				{
				case EDnaExprOperator::Eq: Result = FDnaValue::FromBool(Left.Equals(Right)); break;
				case EDnaExprOperator::Ne: Result = FDnaValue::FromBool(!Left.Equals(Right)); break;
				case EDnaExprOperator::Lt: Result = FDnaValue::FromBool(L < R); break;
				case EDnaExprOperator::Le: Result = FDnaValue::FromBool(L <= R); break;
				case EDnaExprOperator::Gt: Result = FDnaValue::FromBool(L > R); break;
				case EDnaExprOperator::Ge: Result = FDnaValue::FromBool(L >= R); break;
				case EDnaExprOperator::Add: Result = Number(L + R, bInt); break;
				case EDnaExprOperator::Sub: Result = Number(L - R, bInt); break;
				case EDnaExprOperator::Mul: Result = Number(L * R, bInt); break;
				case EDnaExprOperator::Div: Result = R != 0.0 ? Number(bInt ? FMath::TruncToDouble(L / R) : L / R, bInt) : Number(0.0, bInt); break;
				// x % 0 is 0 by definition here; x % -1 is 0 in arithmetic, and INT_MIN % -1 traps on x86.
				case EDnaExprOperator::Mod: Result = FDnaValue::FromInt(Right.Int == 0 || Right.Int == -1 ? 0 : Left.Int % Right.Int); break;
				default: break;
				}
				return Report(&Left, &Right, Result);
			}
			case EDnaExprNodeKind::In:
			{
				const FDnaValue Subject = Child(0);
				for (int32 Which = 1; Which < Node.Children.Num(); ++Which)
				{
					if (Subject.Equals(Child(Which)))
					{
						return Report(&Subject, nullptr, FDnaValue::FromBool(true));
					}
				}
				return Report(&Subject, nullptr, FDnaValue::FromBool(false));
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
