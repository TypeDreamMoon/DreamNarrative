// Copyright DreamMoon. All Rights Reserved.
//
// Reference and type checking over a whole program. Nothing here changes the tree; it only reports.
// Codes raised (draft numbers, Docs/Diagnostics.md is authoritative once filled):
//
//   DNS3001  a fact nobody declared
//   DNS3002  a speaker that is not a declared character
//   DNS3003  a jump or `visited` target that is not a dialogue
//   DNS3005  a quest that does not exist
//   DNS3006  a stage that does not exist
//   DNS3009  a chapter that does not exist
//   DNS4001  a type mismatch
//   DNS4002  a tag value that is not in the fact's declared set
//   DNS4003  a constant outside the fact's range
//   DNS4004  values that cannot be ordered (`<` on tags or bools)
//   DNS4005  a built-in called with the wrong arguments
//   DNS2010  `else` / `else if` with no `if` before it
//   DNS5008  a choice group with no unconditional, non-`once` option

#pragma once

#include "CoreMinimal.h"
#include "Lang/DnaDiagnostic.h"
#include "Lang/DnaModel.h"

namespace UE::DreamNarrative::Lang
{
	/** The static type of an expression as the validator sees it. */
	struct FDnaTypeInfo
	{
		enum class EKind : uint8 { Unknown, Bool, Int, Float, Tag, Chapter };
		EKind Kind = EKind::Unknown;
		/** For Tag: the fact whose domain the value belongs to (null when it came from a bare member). */
		const FDnaFactInfo* TagFact = nullptr;

		bool IsNumeric() const { return Kind == EKind::Int || Kind == EKind::Float; }
		bool IsUnknown() const { return Kind == EKind::Unknown; }
		static FDnaTypeInfo Of(EKind InKind) { FDnaTypeInfo T; T.Kind = InKind; return T; }
		static FDnaTypeInfo OfFact(const FDnaFactInfo& Fact);
		const TCHAR* Describe() const;
	};

	/** Returns false when any error was recorded. */
	DREAMNARRATIVECOMPILER_API bool ValidateDnaProgram(const FDnaProgram& Program, FDnaDiagnosticSink& Diagnostics);
}
