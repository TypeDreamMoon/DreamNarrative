// Copyright DreamMoon. All Rights Reserved.

using UnrealBuildTool;

public class DreamNarrativeStory : ModuleRules
{
	public DreamNarrativeStory(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		// Dialogue, quests and chapters. All three are thin shells over the ledger: dialogue reads
		// and writes facts, quests watch them, chapters gate on them. No UI ships here - the runtime
		// hands out data and events, and the game decides how to draw it.
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"GameplayTags",
			"DreamNarrativeCore",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"EnhancedInput",
			"LevelSequence",
			"MovieScene",
		});
	}
}
