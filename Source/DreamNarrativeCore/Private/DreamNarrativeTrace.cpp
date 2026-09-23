// Copyright DreamMoon. All Rights Reserved.

#include "DreamNarrativeTrace.h"

const TCHAR* LexToString(EDnaFactWriteResult Result)
{
	switch (Result)
	{
	case EDnaFactWriteResult::Written: return TEXT("Written");
	case EDnaFactWriteResult::Unchanged: return TEXT("Unchanged");
	case EDnaFactWriteResult::Clamped: return TEXT("Clamped");
	case EDnaFactWriteResult::RefusedUndeclared: return TEXT("RefusedUndeclared");
	case EDnaFactWriteResult::RefusedOwnership: return TEXT("RefusedOwnership");
	case EDnaFactWriteResult::RefusedType: return TEXT("RefusedType");
	case EDnaFactWriteResult::RefusedDomain: return TEXT("RefusedDomain");
	}
	return TEXT("?");
}
