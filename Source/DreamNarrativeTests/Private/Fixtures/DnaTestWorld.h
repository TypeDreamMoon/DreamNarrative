// Copyright DreamMoon. All Rights Reserved.
//
// The logic-layer fixture: a narrative with no engine world under it. Each FDnaTestWorld has its own
// game instance, ledger and Story subsystem (nothing is shared between tests), a manual clock, a
// fixed random seed, and the test's sources compiled in memory -- nothing is written under /Game.
//
// Steps (Talk, Start, SetExtern, Given, Wait, Jump) are what a player and the game do. After every
// step the fixture saves the ledger, loads the bytes into a fresh ledger and compares the two, so any
// test doubles as a save/load test; a difference fails the test and names the item.

#pragma once

#include "CoreMinimal.h"
#include "DreamNarrativeClock.h"
#include "DreamNarrativeTypes.h"
#include "Generation/DnaGenerator.h"
#include "Lang/DnaModel.h"
#include "UObject/GCObject.h"

class FAutomationTestBase;
class UDreamNarrativeDialogueRunner;
class UDreamNarrativeLedger;
class UDreamNarrativeScript;
class UDreamNarrativeStory;
class UGameInstance;
struct FDnaLedgerSnapshot;

namespace UE::DreamNarrative::Tests
{
	/** The plugin's base directory, absolute. */
	FString GetPluginDir();

	/** One source to compile: a path (it names the asset and the diagnostics) and its text. */
	struct FDnaTestSource
	{
		FString Path;
		FString Text;
	};

	/** Differences between two snapshots, one readable line each; empty when they agree. */
	TArray<FString> DiffSnapshots(const FDnaLedgerSnapshot& Expected, const FDnaLedgerSnapshot& Actual);

	/** Reads files given relative to the plugin directory. A file that cannot be read fails the test. */
	bool ReadPluginSources(FAutomationTestBase& Test, const TArray<FString>& RelativePaths, TArray<FDnaTestSource>& OutSources);

	/**
	 * Parses, checks and generates the sources as one program, the way the commandlet does, and hands
	 * out the scripts. Error diagnostics fail the test and make this return false.
	 */
	bool CompileDnaSources(FAutomationTestBase& Test, const TArray<FDnaTestSource>& Sources, const Lang::FDnaGenerateOptions& Options,
		Lang::FDnaProgram& OutProgram, TArray<UDreamNarrativeScript*>& OutScripts);

	class FDnaTestWorld : public FGCObject
	{
	public:
		/** The seed of `random` and `chance` unless a test sets its own through the ledger. */
		static constexpr int32 DefaultSeed = 7;

		explicit FDnaTestWorld(FAutomationTestBase& InTest);
		virtual ~FDnaTestWorld() override;

		// ---------------------------------------------------------------- building

		/**
		 * Compiles the sources as one program and registers every script. Errors fail the test and
		 * make this return false. A world compiles once.
		 */
		bool Compile(const TArray<FDnaTestSource>& Sources);

		/** Same, reading files given relative to the plugin directory. */
		bool CompilePluginFiles(const TArray<FString>& RelativePaths);

		/** Same, for one source held in the test. */
		bool CompileText(const FString& Text, const FString& Path = TEXT("Inline.dna"));

		UDreamNarrativeLedger& Ledger() const { return *LedgerObject; }
		UDreamNarrativeStory& Story() const { return *StoryObject; }
		FDnaManualClock& Clock() const { return *ManualClock; }
		const Lang::FDnaProgram& Program() const { return ProgramModel; }
		const TArray<TObjectPtr<UDreamNarrativeScript>>& GetScripts() const { return Scripts; }

		// ---------------------------------------------------------------- steps

		/**
		 * Talks to a character and drives the dialogue to its end: lines are advanced, waits run
		 * on the clock, blocking stages finish at once, and each choice point takes the next of
		 * Choices, matched by label or by `#id`. A choice point with no pick left, a pick that is not
		 * on offer, or a pick never offered fails the test.
		 */
		bool Talk(FName Character, const TArray<FString>& Choices = {});

		/** Starts a dialogue node by name and drives it the same way. */
		bool Start(FName Node, const TArray<FString>& Choices = {});

		/** Writes a fact the way game code does: through the ownership rule. */
		bool SetExtern(FName Fact, const FDnaValue& Value);

		/** Writes any fact with the narrative's authority: a test's starting state. */
		bool Given(FName Fact, const FDnaValue& Value);

		/** Moves narrative time forward, firing what falls due. */
		void Wait(double Seconds);

		/** The debug teleport: implied facts along the `after` chain, then the chapter. */
		bool Jump(FName Chapter);

		// ---------------------------------------------------------------- looking

		/**
		 * Compiles an expression in the source language against the program without failing the test.
		 * Returns the expression's index in the fixture's own pool, INDEX_NONE with OutError set when
		 * it does not parse or check.
		 */
		int32 CompileExpression(const FString& Expression, bool bAsCondition, FString& OutError);

		/** Evaluates an expression against the ledger. A compile error fails the test. */
		bool Evaluate(const FString& Expression, FDnaValue& OutValue);

		/** Fails the test when the condition is false (listing the facts it reads) or does not compile. */
		bool Expect(const FString& Condition);

		/** Everything shown and done so far, across dialogues and steps, one entry per event. */
		const TArray<FString>& GetTranscript() const { return Transcript; }
		bool TranscriptHas(const FString& Needle) const;

		/** The transcript of the dialogue driven last. */
		const TArray<FString>& GetLastDialogue() const { return LastDialogue; }
		bool LastDialogueHas(const FString& Needle) const;

		/**
		 * The save check on its own: snapshot, serialise with an archive that writes SaveGame
		 * properties only, load the bytes into a fresh ledger that knows the same scripts, compare.
		 */
		bool CheckSaveRoundTrip(const FString& Where);

		/** Every step runs CheckSaveRoundTrip unless this is cleared. */
		bool bCheckSaveAfterEachStep = true;

		// ---------------------------------------------------------------- FGCObject

		virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
		virtual FString GetReferencerName() const override { return TEXT("UE::DreamNarrative::Tests::FDnaTestWorld"); }

	private:
		bool Drive(UDreamNarrativeDialogueRunner* Runner, const TArray<FString>& Choices, const FString& What);
		bool AfterStep(const FString& What, bool bStepOk);
		FString DescribeFactsOf(const FString& Expression) const;

		FAutomationTestBase& Test;
		TObjectPtr<UGameInstance> GameInstance;
		TObjectPtr<UDreamNarrativeLedger> LedgerObject;
		TObjectPtr<UDreamNarrativeStory> StoryObject;
		TArray<TObjectPtr<UDreamNarrativeScript>> Scripts;
		/** Holds the expressions a test compiles; never registered with the ledger. */
		TObjectPtr<UDreamNarrativeScript> ExpressionPool;
		TSharedPtr<FDnaManualClock> ManualClock;
		Lang::FDnaProgram ProgramModel;
		bool bCompiled = false;
		TArray<FString> Transcript;
		TArray<FString> LastDialogue;
	};
}
