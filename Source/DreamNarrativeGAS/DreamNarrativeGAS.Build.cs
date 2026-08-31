// Copyright DreamMoon. All Rights Reserved.

using UnrealBuildTool;

public class DreamNarrativeGAS : ModuleRules
{
	public DreamNarrativeGAS(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		// Optional bridge to the Gameplay Ability System: ability tags and gameplay events feed the
		// ledger, quest completion sends gameplay events back, and an ability can start a stage.
		// Nothing in Core or Story includes this module, so a project without GAS drops it and the
		// rest still builds.
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"GameplayTags",
			"GameplayAbilities",
			"GameplayTasks",
			"DreamNarrativeCore",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"DreamNarrativeStory",
			"DreamNarrativeStage",
		});
	}
}
