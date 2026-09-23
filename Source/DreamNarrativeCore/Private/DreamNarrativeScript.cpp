// Copyright DreamMoon. All Rights Reserved.

#include "DreamNarrativeScript.h"
#include "Misc/SecureHash.h"

UDreamNarrativeScript* UDreamNarrativeScriptSection::GetScript() const
{
	return GetTypedOuter<UDreamNarrativeScript>();
}

UDreamNarrativeScriptSection* UDreamNarrativeScript::FindSection(TSubclassOf<UDreamNarrativeScriptSection> Class) const
{
	for (const TObjectPtr<UDreamNarrativeScriptSection>& Section : Sections)
	{
		if (Section && Class && Section->IsA(Class))
		{
			return Section;
		}
	}
	return nullptr;
}

FString UDreamNarrativeScript::HashSource(const FString& Source)
{
	return FMD5::HashAnsiString(*Source);
}
