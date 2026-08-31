// Copyright DreamMoon. All Rights Reserved.

using UnrealBuildTool;

public class DreamNarrativeCore : ModuleRules
{
	public DreamNarrativeCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		// The fact ledger, the expression language and the event bus. Everything the narrative knows
		// about the world lives here; story, stage and every adapter are consumers. Keep this module
		// free of anything gameplay-specific - it must stay usable by a project that wants only the
		// ledger.
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"GameplayTags",
			"DeveloperSettings",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Projects",
		});
	}
}
