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
#include "DreamNarrativeLedger.h"
#include "DreamNarrativeTypes.h"
#include "Generation/DnaGenerator.h"
#include "Lang/DnaModel.h"
#include "UObject/GCObject.h"

class FAutomationTestBase;
class UDreamNarrativeDialogueRunner;
class UDreamNarrativeScript;
class UDreamNarrativeStageSubsystem;
class UDreamNarrativeStory;
class UGameInstance;

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

	/**
	 * The dialogue a driver moves on next: the first channel's (in channel order) that runs, is not
	 * paused and has something to show; failing that, the first one waiting on the clock. Null when
	 * every channel is quiet or held still.
	 */
	UDreamNarrativeDialogueRunner* NextRunnerToDrive(const UDreamNarrativeStory& Story);

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

		/**
		 * Puts the Stage module's host on the ledger, with nobody to show a stage to: `play`, `@` and
		 * `stage: pause` go through it and a stage takes its time on the clock, unless the dialogue
		 * moves on first (the steps finish blocking stages at once). Off by default: without a host the
		 * story only tells the game, as in a project without Stage.
		 */
		UDreamNarrativeStageSubsystem* EnableStages();
		UDreamNarrativeStageSubsystem* GetStages() const { return StagesObject; }

		/** The game instance the world's subsystems live in: the outer for any other one a test makes. */
		UGameInstance* GetGameInstance() const { return GameInstance; }
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

		/**
		 * Raises an event the way game code does, then lets every channel say everything it has (lines
		 * move on at once). A choice on any channel fails the test: this step has no picks.
		 */
		bool Emit(const FDnaEventPayload& Event);

		/** Writes a fact the way game code does: through the ownership rule. */
		bool SetExtern(FName Fact, const FDnaValue& Value);

		/** Writes any fact with the narrative's authority: a test's starting state. */
		bool Given(FName Fact, const FDnaValue& Value);

		/** Moves narrative time forward, firing what falls due. */
		void Wait(double Seconds);

		/** The debug teleport: implied facts along the `after` chain, then the chapter. */
		bool Jump(FName Chapter);

		/** The next draws of `random` and `chance` come out as these units, in order; then the seeded stream again. */
		void ForceRolls(const TArray<float>& Units) { Rolls.Queue.Append(Units); }

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

		/** What CheckSaveRoundTrip compares, one line per difference; empty when the ledger comes back the same. */
		TArray<FString> SaveRoundTripDifferences();

		/** Every step runs CheckSaveRoundTrip unless this is cleared. */
		bool bCheckSaveAfterEachStep = true;

		// ---------------------------------------------------------------- for step runners

		/** Put in front of every failure this world reports, e.g. `Samples/Ring.dna(12)`; empty for none. */
		void SetContext(const FString& InContext) { Context = InContext; }

		/** Reports a failure with the current context. */
		void ReportError(const FString& Message);

		/** From now on failures go into Into instead of the test (null: back to the test). For checking that something fails. */
		void CaptureErrors(TArray<FString>* Into) { ErrorCapture = Into; }

		/** Lowers an expression of the compiled program (already checked) into the world's pool; INDEX_NONE if it cannot be. */
		int32 CompileAst(const Lang::FDnaExprPtr& Expr);

		/** An expression CompileAst or CompileExpression put in the pool. */
		const FDnaCompiledExpr* GetPooledExpression(int32 Index) const;

		FAutomationTestBase& GetTest() const { return Test; }

		/** Evaluates an expression of the compiled program (already checked) against the ledger. */
		bool EvaluateAst(const Lang::FDnaExprPtr& Expr, FDnaValue& OutValue);

		/** Same, for a value written to Fact: a bare member of a tag fact's set means that member. */
		bool EvaluateValueFor(const Lang::FDnaExprPtr& Expr, const FString& Fact, FDnaValue& OutValue);

		/** Same, for a value given to parameter Param of Event (an emit's argument). */
		bool EvaluateValueForParam(const Lang::FDnaExprPtr& Expr, const FString& Event, const FString& Param, FDnaValue& OutValue);

		/** "Now: fact = value, ..." for the facts (and the chapter) an expression reads. */
		FString DescribeFactsOf(const Lang::FDnaExpr& Expr) const;

		/** Ends a step a runner drove itself: the save check, unless turned off. */
		bool FinishStep(const FString& What, bool bStepOk);

		/** Adds a finished dialogue's transcript to the world's, making it the last dialogue. */
		void RecordDialogue(const UDreamNarrativeDialogueRunner& Runner);

		// ---------------------------------------------------------------- FGCObject

		virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
		virtual FString GetReferencerName() const override { return TEXT("UE::DreamNarrative::Tests::FDnaTestWorld"); }

	private:
		/** The world's draws: forced rolls first, then the ledger's own seeded stream. */
		struct FForcedRolls : IDnaRandomSource
		{
			TArray<float> Queue;
			virtual float NextUnit(FRandomStream& Stream) override
			{
				if (Queue.Num() == 0)
				{
					return Stream.GetFraction();
				}
				const float Unit = Queue[0];
				Queue.RemoveAt(0);
				return Unit;
			}
		};

		/**
		 * Lets every channel say everything it has: lines move on at once, waits run on the clock once
		 * nothing else is left to show, stages finish at once. Choices on Runner take the picks in order;
		 * choices anywhere else fail the test.
		 */
		bool Drive(UDreamNarrativeDialogueRunner* Runner, const TArray<FString>& Choices, const FString& What);

		FAutomationTestBase& Test;
		TObjectPtr<UGameInstance> GameInstance;
		TObjectPtr<UDreamNarrativeLedger> LedgerObject;
		TObjectPtr<UDreamNarrativeStory> StoryObject;
		TObjectPtr<UDreamNarrativeStageSubsystem> StagesObject;
		TArray<TObjectPtr<UDreamNarrativeScript>> Scripts;
		/** Holds the expressions a test compiles; never registered with the ledger. */
		TObjectPtr<UDreamNarrativeScript> ExpressionPool;
		TSharedPtr<FDnaManualClock> ManualClock;
		Lang::FDnaProgram ProgramModel;
		bool bCompiled = false;
		TArray<FString> Transcript;
		TArray<FString> LastDialogue;
		FString Context;
		FForcedRolls Rolls;
		TArray<FString>* ErrorCapture = nullptr;
	};
}
