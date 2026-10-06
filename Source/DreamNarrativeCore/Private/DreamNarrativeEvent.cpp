// Copyright DreamMoon. All Rights Reserved.

#include "DreamNarrativeEvent.h"

const FDnaValue* FDnaEventPayload::Find(FName Param) const
{
	const FDnaEventArg* Arg = Args.FindByPredicate([Param](const FDnaEventArg& Each) { return Each.Name == Param; });
	return Arg ? &Arg->Value : nullptr;
}

void FDnaEventPayload::Set(FName Param, const FDnaValue& Value)
{
	if (FDnaEventArg* Arg = Args.FindByPredicate([Param](const FDnaEventArg& Each) { return Each.Name == Param; }))
	{
		Arg->Value = Value;
		return;
	}
	Args.Add({ Param, Value });
}

FString FDnaEventPayload::ToString() const
{
	if (Args.Num() == 0)
	{
		return Event.ToString();
	}
	TArray<FString> Parts;
	for (const FDnaEventArg& Arg : Args)
	{
		Parts.Add(FString::Printf(TEXT("%s = %s"), *Arg.Name.ToString(), *Arg.Value.ToString()));
	}
	return FString::Printf(TEXT("%s(%s)"), *Event.ToString(), *FString::Join(Parts, TEXT(", ")));
}

const FDnaFactDecl* FDnaEventDecl::FindParam(FName Param) const
{
	return Params.FindByPredicate([Param](const FDnaFactDecl& Each) { return Each.Name == Param; });
}

bool FDnaEventScopeContext::GetEventParam(FName InEvent, FName Param, FDnaValue& OutValue) const
{
	if (Event && Event->Event == InEvent)
	{
		if (const FDnaValue* Value = Event->Find(Param))
		{
			OutValue = *Value;
			return true;
		}
	}
	return Inner.GetEventParam(InEvent, Param, OutValue);
}
