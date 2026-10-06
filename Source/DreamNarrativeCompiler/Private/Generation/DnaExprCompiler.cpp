// Copyright DreamMoon. All Rights Reserved.

#include "Generation/DnaExprCompiler.h"
#include "DreamNarrativeScript.h"

namespace UE::DreamNarrative::Lang
{
	FDnaExprCompiler::FDnaExprCompiler(const FDnaProgram& InProgram, UDreamNarrativeScript& InScript)
		: Program(InProgram), Script(InScript)
	{
	}

	int32 FDnaExprCompiler::Compile(const FDnaExprPtr& Expr)
	{
		return CompileValueFor(Expr, nullptr);
	}

	int32 FDnaExprCompiler::CompileValueFor(const FDnaExprPtr& Expr, const FDnaFactInfo* TargetFact)
	{
		if (!Expr.IsValid())
		{
			return INDEX_NONE;
		}
		FDnaCompiledExpr Compiled;
		Compiled.Source = Expr->ToString();
		Compiled.Root = Lower(*Expr, Compiled, TargetFact, false);
		if (Compiled.Root == INDEX_NONE)
		{
			return INDEX_NONE;
		}
		return Script.Expressions.Add(MoveTemp(Compiled));
	}

	bool FDnaExprCompiler::Constant(const FDnaExprPtr& Expr, const FDnaFactInfo* TargetFact, FDnaValue& OutValue) const
	{
		if (!Expr.IsValid())
		{
			return false;
		}
		FDnaCompiledExpr Compiled;
		Compiled.Root = Lower(*Expr, Compiled, TargetFact, false);
		return ::DreamNarrative::EvaluateConstant(Compiled, OutValue);
	}

	int32 FDnaExprCompiler::CountConjuncts(const FDnaExprPtr& Expr)
	{
		if (!Expr.IsValid())
		{
			return 0;
		}
		if (Expr->Kind == EDnaExprKind::Binary && Expr->Op == EDnaExprOp::And && Expr->Args.Num() == 2)
		{
			return CountConjuncts(Expr->Args[0]) + CountConjuncts(Expr->Args[1]);
		}
		return 1;
	}

	const FDnaFactInfo* FDnaExprCompiler::EventParamOf(const FDnaExpr& Expr) const
	{
		if (Expr.Kind != EDnaExprKind::Name || Expr.Path.Num() != 2)
		{
			return nullptr;
		}
		const FDnaEventInfo* Event = Program.FindEvent(Expr.Path[0]);
		return Event ? Event->FindParam(Expr.Path[1]) : nullptr;
	}

	const FDnaFactInfo* FDnaExprCompiler::TagFactOf(const FDnaExpr& Expr) const
	{
		if (Expr.Kind != EDnaExprKind::Name)
		{
			return nullptr;
		}
		const FDnaFactInfo* Fact = Program.FindFact(Expr.Name);
		if (!Fact)
		{
			Fact = EventParamOf(Expr);
		}
		return (Fact && Fact->Type == EDnaFactType::Tag) ? Fact : nullptr;
	}

	int32 FDnaExprCompiler::Lower(const FDnaExpr& Expr, FDnaCompiledExpr& Out, const FDnaFactInfo* TagContext, bool bChapterContext) const
	{
		auto Push = [&Out](const FDnaExprNode& Node) -> int32 { return Out.Nodes.Add(Node); };

		switch (Expr.Kind)
		{
		case EDnaExprKind::Literal:
		{
			FDnaExprNode Node;
			Node.Kind = EDnaExprNodeKind::Literal;
			Node.Literal = Expr.Literal;
			return Push(Node);
		}
		case EDnaExprKind::Chapter:
		{
			FDnaExprNode Node;
			Node.Kind = EDnaExprNodeKind::Chapter;
			return Push(Node);
		}
		case EDnaExprKind::Name:
		{
			FDnaExprNode Node;
			if (Expr.Path.Num() == 2 && Program.IsQuest(Expr.Path[0]))
			{
				Node.Kind = EDnaExprNodeKind::QuestState;
				Node.Name = FName(*Expr.Path[0]);
				const FString& Query = Expr.Path[1];
				Node.QuestQuery = Query == TEXT("available") ? EDnaQuestStateQuery::Available
					: Query == TEXT("active") ? EDnaQuestStateQuery::Active
					: Query == TEXT("failed") ? EDnaQuestStateQuery::Failed
					: EDnaQuestStateQuery::Completed;
				return Push(Node);
			}
			if (Program.FindFact(Expr.Name))
			{
				Node.Kind = EDnaExprNodeKind::Fact;
				Node.Name = FName(*Expr.Name);
				return Push(Node);
			}
			if (EventParamOf(Expr))
			{
				Node.Kind = EDnaExprNodeKind::EventParam;
				Node.Event = FName(*Expr.Path[0]);
				Node.Name = FName(*Expr.Path[1]);
				return Push(Node);
			}
			if (TagContext && Expr.Path.Num() == 1 && TagContext->HasMember(Expr.Name))
			{
				Node.Kind = EDnaExprNodeKind::Member;
				Node.Name = FName(*Expr.Name);
				return Push(Node);
			}
			if (bChapterContext && Expr.Path.Num() == 1 && Program.IsChapter(Expr.Name))
			{
				Node.Kind = EDnaExprNodeKind::ChapterName;
				Node.Name = FName(*Expr.Name);
				return Push(Node);
			}
			// The validator has already reported this; keep the shape so the pool stays consistent.
			Node.Kind = EDnaExprNodeKind::Fact;
			Node.Name = FName(*Expr.Name);
			return Push(Node);
		}
		case EDnaExprKind::Call:
		{
			FDnaExprNode Node;
			if (Expr.Name == TEXT("visited"))
			{
				Node.Kind = EDnaExprNodeKind::Visited;
				Node.Name = Expr.Args.Num() > 0 && Expr.Args[0].IsValid() ? FName(*Expr.Args[0]->Name) : NAME_None;
				return Push(Node);
			}
			if (Expr.Name == TEXT("random")) { Node.Kind = EDnaExprNodeKind::Random; }
			else if (Expr.Name == TEXT("chance")) { Node.Kind = EDnaExprNodeKind::Chance; }
			else { return INDEX_NONE; }
			TArray<int32> Children;
			for (const FDnaExprPtr& Arg : Expr.Args)
			{
				if (!Arg.IsValid()) { return INDEX_NONE; }
				Children.Add(Lower(*Arg, Out, nullptr, false));
			}
			Node.Children = MoveTemp(Children);
			return Push(Node);
		}
		case EDnaExprKind::Unary:
		{
			if (Expr.Args.Num() != 1 || !Expr.Args[0].IsValid()) { return INDEX_NONE; }
			const int32 Operand = Lower(*Expr.Args[0], Out, TagContext, bChapterContext);
			FDnaExprNode Node;
			Node.Kind = EDnaExprNodeKind::Unary;
			Node.Operator = Expr.Op == EDnaExprOp::Not ? EDnaExprOperator::Not : EDnaExprOperator::Neg;
			Node.Children = { Operand };
			return Push(Node);
		}
		case EDnaExprKind::Binary:
		{
			if (Expr.Args.Num() != 2 || !Expr.Args[0].IsValid() || !Expr.Args[1].IsValid()) { return INDEX_NONE; }
			const FDnaExpr& Left = *Expr.Args[0];
			const FDnaExpr& Right = *Expr.Args[1];

			// Equality against a tag fact or the chapter gives the other side its member context.
			const FDnaFactInfo* LeftTag = nullptr;
			const FDnaFactInfo* RightTag = nullptr;
			bool bLeftChapter = false;
			bool bRightChapter = false;
			if (Expr.Op == EDnaExprOp::Eq || Expr.Op == EDnaExprOp::Ne)
			{
				RightTag = TagFactOf(Left);
				LeftTag = TagFactOf(Right);
				bRightChapter = Left.Kind == EDnaExprKind::Chapter;
				bLeftChapter = Right.Kind == EDnaExprKind::Chapter;
			}
			const int32 L = Lower(Left, Out, LeftTag ? LeftTag : TagContext, bLeftChapter || bChapterContext);
			const int32 R = Lower(Right, Out, RightTag ? RightTag : TagContext, bRightChapter || bChapterContext);

			FDnaExprNode Node;
			Node.Kind = EDnaExprNodeKind::Binary;
			switch (Expr.Op)
			{
			case EDnaExprOp::Or: Node.Operator = EDnaExprOperator::Or; break;
			case EDnaExprOp::And: Node.Operator = EDnaExprOperator::And; break;
			case EDnaExprOp::Eq: Node.Operator = EDnaExprOperator::Eq; break;
			case EDnaExprOp::Ne: Node.Operator = EDnaExprOperator::Ne; break;
			case EDnaExprOp::Lt: Node.Operator = EDnaExprOperator::Lt; break;
			case EDnaExprOp::Le: Node.Operator = EDnaExprOperator::Le; break;
			case EDnaExprOp::Gt: Node.Operator = EDnaExprOperator::Gt; break;
			case EDnaExprOp::Ge: Node.Operator = EDnaExprOperator::Ge; break;
			case EDnaExprOp::Add: Node.Operator = EDnaExprOperator::Add; break;
			case EDnaExprOp::Sub: Node.Operator = EDnaExprOperator::Sub; break;
			case EDnaExprOp::Mul: Node.Operator = EDnaExprOperator::Mul; break;
			case EDnaExprOp::Div: Node.Operator = EDnaExprOperator::Div; break;
			case EDnaExprOp::Mod: Node.Operator = EDnaExprOperator::Mod; break;
			default: return INDEX_NONE;
			}
			Node.Children = { L, R };
			return Push(Node);
		}
		case EDnaExprKind::In:
		{
			if (Expr.Args.Num() < 2 || !Expr.Args[0].IsValid()) { return INDEX_NONE; }
			const FDnaExpr& Subject = *Expr.Args[0];
			const FDnaFactInfo* SubjectTag = TagFactOf(Subject);
			const bool bSubjectChapter = Subject.Kind == EDnaExprKind::Chapter;
			TArray<int32> Children;
			Children.Add(Lower(Subject, Out, TagContext, bChapterContext));
			for (int32 Index = 1; Index < Expr.Args.Num(); ++Index)
			{
				if (!Expr.Args[Index].IsValid()) { return INDEX_NONE; }
				Children.Add(Lower(*Expr.Args[Index], Out, SubjectTag ? SubjectTag : TagContext, bSubjectChapter || bChapterContext));
			}
			FDnaExprNode Node;
			Node.Kind = EDnaExprNodeKind::In;
			Node.Children = MoveTemp(Children);
			return Push(Node);
		}
		}
		return INDEX_NONE;
	}
}
