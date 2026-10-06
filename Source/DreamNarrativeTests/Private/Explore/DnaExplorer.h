// Copyright DreamMoon. All Rights Reserved.
//
// The explorer: breadth-first over every state a story can reach from where a test world stands.
//
// A state is the ledger between two moves (facts, chapter, visits, once-choices). A move is one of
// the things a player or the game can do there: talk to a character, start a dialogue only the game
// can start, write an extern fact to one of its key values, or raise an event some dialogue opens on
// (with key values for the parameters its dialogues read). After a move every channel says all it
// has, so between two moves nothing talks. A move is played once per way through it -- every choice
// and both ends of every draw -- by replaying it from the state with the decisions forced, so no
// dialogue position ever has to be saved.
//
// Numbers only ever compared with constants and only ever moving one way (a counter, a visit count)
// are folded at the farthest boundary any comparison draws, and what no condition reads is left out
// of the state, so a story whose counters grow forever still has finitely many states.
//
// Along the way it checks what every story must get right (runtime errors, dialogues that cannot
// stop or cannot be left, saves) and afterwards what the explore block asked (always, eventually,
// softlock), and it lists content it never saw.

#pragma once

#include "CoreMinimal.h"

namespace UE::DreamNarrative::Tests
{
	class FDnaTestWorld;
}

namespace UE::DreamNarrative::Tests::Explore
{
	enum class EDnaGoalKind : uint8
	{
		/** Holds in every state reached. */
		Always,
		/** Holds in some state reached. */
		Eventually,
		/** Can be reached from the start, and from every state reached. */
		Softlock,
	};

	struct FDnaExploreGoal
	{
		EDnaGoalKind Kind = EDnaGoalKind::Always;
		/** Index into the world's expression pool. */
		int32 Expr = INDEX_NONE;
		/** The condition as written, for messages and repros. */
		FString Source;
		/** `file(line)` of the property. */
		FString Where;
	};

	struct FDnaExploreOptions
	{
		int32 MaxDepth = 40;
		int32 MaxStates = 100000;
		double MaxSeconds = 60.0;
		TArray<FDnaExploreGoal> Goals;
	};

	struct FDnaExploreFinding
	{
		/** DNS5101 ... DNS5110, DNS9201. */
		FString Code;
		bool bWarning = false;
		FString Message;
		/** The steps that lead to it from the start of the exploration, as block lines at step level; empty when there is no path to give. */
		TArray<FString> ReproSteps;
		/** The line the repro block ends with: `expect ...`, `eventually ...`, `save`; empty for none. */
		FString ReproTail;
		/** The repro is an explore block (a softlock), not a test block. */
		bool bReproIsExplore = false;
		/** Appended to the explore block's name to name the repro. */
		FString ReproSuffix;
	};

	struct FDnaExploreStats
	{
		int32 States = 0;
		int32 Transitions = 0;
		int32 Runs = 0;
		int32 Depth = 0;
		double Seconds = 0.0;
		/** Every state reached was fully explored. */
		bool bExhausted = false;
		/** Which limit stopped it, when one did. */
		FString LimitHit;
	};

	class FDnaExplorer
	{
	public:
		/** The world must be compiled and standing where the exploration starts. */
		FDnaExplorer(FDnaTestWorld& World, const FDnaExploreOptions& Options);
		~FDnaExplorer();

		/** Explores, then checks the goals. Leaves the world's ledger in the state it started in. */
		void Run();

		const TArray<FDnaExploreFinding>& GetFindings() const;
		const FDnaExploreStats& GetStats() const;

	private:
		class FImpl;
		TUniquePtr<FImpl> Impl;
	};
}
