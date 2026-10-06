// Copyright DreamMoon. All Rights Reserved.

#include "DreamNarrativeTypes.h"

bool FDnaValue::AsBool() const
{
	switch (Type)
	{
	case EDnaFactType::Bool: return bBool;
	case EDnaFactType::Int: return Int != 0;
	case EDnaFactType::Float: return Float != 0.f;
	case EDnaFactType::Tag: return !Tag.IsNone();
	}
	return false;
}

double FDnaValue::AsNumber() const
{
	switch (Type)
	{
	case EDnaFactType::Bool: return bBool ? 1.0 : 0.0;
	case EDnaFactType::Int: return static_cast<double>(Int);
	case EDnaFactType::Float: return static_cast<double>(Float);
	case EDnaFactType::Tag: return 0.0;
	}
	return 0.0;
}

bool FDnaValue::Equals(const FDnaValue& Other) const
{
	if (IsNumeric() && Other.IsNumeric())
	{
		return FMath::IsNearlyEqual(AsNumber(), Other.AsNumber(), 1e-6);
	}
	if (Type != Other.Type)
	{
		return false;
	}
	switch (Type)
	{
	case EDnaFactType::Bool: return bBool == Other.bBool;
	case EDnaFactType::Tag: return Tag == Other.Tag;
	default: return false;
	}
}

FString FDnaValue::ToString() const
{
	switch (Type)
	{
	case EDnaFactType::Bool: return bBool ? TEXT("true") : TEXT("false");
	case EDnaFactType::Int: return FString::FromInt(Int);
	case EDnaFactType::Float: return FString::SanitizeFloat(Float);
	case EDnaFactType::Tag: return Tag.ToString();
	}
	return FString();
}

namespace DreamNarrative
{
	namespace Tags
	{
		FString MakeFactTagString(FName FactName)
		{
			return FString::Printf(TEXT("Narrative.Fact.%s"), *FactName.ToString());
		}

		FString MakeEnumTagString(FName FactName, FName Member)
		{
			return FString::Printf(TEXT("Narrative.Enum.%s.%s"), *FactName.ToString(), *Member.ToString());
		}

		FString MakeChapterTagString(FName ChapterName)
		{
			return FString::Printf(TEXT("Narrative.Chapter.%s"), *ChapterName.ToString());
		}

		FString MakeEventTagString(FName EventName)
		{
			return FString::Printf(TEXT("Narrative.Event.%s"), *EventName.ToString());
		}
	}

	const TCHAR* LexToString(EDnaFactType Type)
	{
		switch (Type)
		{
		case EDnaFactType::Bool: return TEXT("bool");
		case EDnaFactType::Int: return TEXT("int");
		case EDnaFactType::Float: return TEXT("float");
		case EDnaFactType::Tag: return TEXT("tag");
		}
		return TEXT("?");
	}

	bool TryParseFactType(const FString& Text, EDnaFactType& OutType)
	{
		if (Text == TEXT("bool")) { OutType = EDnaFactType::Bool; return true; }
		if (Text == TEXT("int")) { OutType = EDnaFactType::Int; return true; }
		if (Text == TEXT("float")) { OutType = EDnaFactType::Float; return true; }
		if (Text == TEXT("tag")) { OutType = EDnaFactType::Tag; return true; }
		return false;
	}

	FName QuestStateFact(FName Quest)
	{
		return FName(*(TEXT("@") + Quest.ToString()));
	}

	FName QuestObjectivesFact(FName Quest)
	{
		return FName(*(TEXT("@") + Quest.ToString() + TEXT(".objectives")));
	}

	FName ArchiveFact(FName Archive)
	{
		return FName(*(TEXT("@archive:") + Archive.ToString()));
	}

	FName QuestStateMember(EDnaQuestState State)
	{
		switch (State)
		{
		case EDnaQuestState::Inactive: return TEXT("Inactive");
		case EDnaQuestState::Accepted: return TEXT("Accepted");
		case EDnaQuestState::Going: return TEXT("Going");
		case EDnaQuestState::Completed: return TEXT("Completed");
		case EDnaQuestState::Failed: return TEXT("Failed");
		case EDnaQuestState::TimedOut: return TEXT("TimedOut");
		}
		return TEXT("Inactive");
	}

	bool TryParseQuestState(FName Member, EDnaQuestState& OutState)
	{
		for (const EDnaQuestState State : { EDnaQuestState::Inactive, EDnaQuestState::Accepted, EDnaQuestState::Going,
			EDnaQuestState::Completed, EDnaQuestState::Failed, EDnaQuestState::TimedOut })
		{
			if (QuestStateMember(State) == Member)
			{
				OutState = State;
				return true;
			}
		}
		return false;
	}
}
