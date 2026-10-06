// Copyright DreamMoon. All Rights Reserved.
//
// The archive half of the Story subsystem: entries unlock the first time their condition holds and
// stay unlocked. Which are unlocked is a ledger fact, so it saves, loads and carries over the way any
// fact of its scope does.

#include "DreamNarrativeArchive.h"
#include "DreamNarrativeStory.h"
#include "DreamNarrativeTrace.h"

const FDnaArchiveData* UDreamNarrativeStory::FindArchive(FName Archive, const UDnaArchiveSection** OutSection) const
{
	const FArchiveRef* Ref = ArchiveIndex.Find(Archive);
	if (!Ref || !Ref->Section.IsValid() || !Ref->Section->Archives.IsValidIndex(Ref->ArchiveIndex))
	{
		return nullptr;
	}
	if (OutSection)
	{
		*OutSection = Ref->Section.Get();
	}
	return &Ref->Section->Archives[Ref->ArchiveIndex];
}

void UDreamNarrativeStory::CheckArchives()
{
	if (!Ledger)
	{
		return;
	}
	for (const FName& Name : ArchiveOrder)
	{
		const UDnaArchiveSection* Section = nullptr;
		const FDnaArchiveData* Archive = FindArchive(Name, &Section);
		const UDreamNarrativeScript* Script = Section ? Section->GetScript() : nullptr;
		if (!Archive || !Script)
		{
			continue;
		}
		const FName Fact = ::DreamNarrative::ArchiveFact(Name);
		const int32 Before = Ledger->GetFactAsInt(Fact);
		int32 Unlocked = Before;
		TArray<int32> Fresh;
		for (int32 Index = 0; Index < Archive->Entries.Num() && Index < 31; ++Index)
		{
			const int32 Bit = 1 << Index;
			const int32 Condition = Archive->Entries[Index].Condition;
			if (!(Unlocked & Bit) && (Condition == INDEX_NONE || Holds(Condition, *Script)))
			{
				Unlocked |= Bit;
				Fresh.Add(Index);
			}
		}
		if (Unlocked == Before)
		{
			continue;
		}
		Ledger->SetFact(Fact, FDnaValue::FromInt(Unlocked), EDnaWriteSource::Narrative);
		for (const int32 Index : Fresh)
		{
			if (IDnaTrace* Tracer = Ledger->GetTrace()) { Tracer->OnEvent(TEXT("archive.unlock")); }
			OnArchiveUnlocked.Broadcast(Name, Index);
		}
	}
}

bool UDreamNarrativeStory::IsArchiveEntryUnlocked(FName Archive, int32 Entry) const
{
	return Ledger && Entry >= 0 && Entry < 31 && (Ledger->GetFactAsInt(::DreamNarrative::ArchiveFact(Archive)) & (1 << Entry)) != 0;
}

FDnaArchiveView UDreamNarrativeStory::GetArchive(FName Archive) const
{
	FDnaArchiveView View;
	const UDnaArchiveSection* Section = nullptr;
	const FDnaArchiveData* Data = FindArchive(Archive, &Section);
	if (!Data || !Section || !Section->GetScript())
	{
		return View;
	}
	View.Name = Data->Name;
	View.Title = Data->Title;
	View.Total = Data->Entries.Num();
	for (int32 Index = 0; Index < Data->Entries.Num(); ++Index)
	{
		if (IsArchiveEntryUnlocked(Archive, Index))
		{
			View.Entries.Add(Format(Data->Entries[Index].Text, *Section->GetScript()));
			View.EntryIndices.Add(Index);
		}
	}
	return View;
}
