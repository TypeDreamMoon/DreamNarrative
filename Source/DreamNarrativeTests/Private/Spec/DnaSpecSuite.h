// Copyright DreamMoon. All Rights Reserved.
//
// The spec suite: every `test` block under the plugin's Tests/Spec and Tests/Samples. A test file is
// compiled on its own, together with the files its `file` block lists under `with:` (paths relative
// to the test file), so a test can drive a sample story without copying it.

#pragma once

#include "CoreMinimal.h"

class FAutomationTestBase;
class IDnaTrace;

namespace UE::DreamNarrative::Tests
{
	class FDnaTestWorld;
}

namespace UE::DreamNarrative::Tests::Spec
{
	/** The plugin's Tests directory, absolute; empty when the plugin is not loaded. */
	FString GetTestsDir();

	/** Every test file of the suite, relative to Tests/ with forward slashes: Spec/... first, then Samples/... */
	void FindTestFiles(TArray<FString>& OutRelative);

	/** Every .dna under Tests/<Directory>, relative to Tests/ with forward slashes, sorted. */
	void FindFilesUnder(const FString& Directory, TArray<FString>& OutRelative);

	/** The names of a file's top-level blocks that start with Keyword (`test`, `explore`), in order, without a full parse. */
	TArray<FString> ScanBlockNames(const FString& Relative, const TCHAR* Keyword);

	/** `Spec/Dialogue/Choices.dna` -> `Dialogue.Choices`; `Samples/Ring.dna` -> `Samples.Ring`. */
	FString ToDisplayPath(const FString& Relative);

	/**
	 * Compiles a test file with its `with:` companions into World. Appended, when given, is compiled as
	 * if it stood at the end of the file (a repro block being tried, say). Failures go to the world's test.
	 */
	bool CompileTestFile(FDnaTestWorld& World, const FString& Relative, const FString& Appended = FString());

	/**
	 * Compiles a test file with its `with:` companions and runs one of its test blocks. Trace, when
	 * given, is installed on the world's ledger for the whole run. Failures go to Test.
	 */
	bool RunSpecTest(FAutomationTestBase& Test, const FString& Relative, const FString& BlockName, IDnaTrace* Trace = nullptr);
}
