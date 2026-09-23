// Copyright DreamMoon. All Rights Reserved.

using UnrealBuildTool;

public class DreamNarrativeTests : ModuleRules
{
	public DreamNarrativeTests(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		// Every automated test of the plugin and the fixtures they share. Tests compile narrative
		// sources in memory, so this module links the compiler.
		//
		// UncookedOnly, not Editor: an Editor module is not loaded when the editor binary runs with
		// -game (GIsEditor is false there), and the smoke tests run in exactly that process. A
		// packaged game never carries this module.
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"GameplayTags",
			"Projects",
			"AssetRegistry",
			"DreamNarrativeCore",
			"DreamNarrativeStory",
			"DreamNarrativeCompiler",
		});
	}
}
