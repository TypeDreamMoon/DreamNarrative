// Copyright DreamMoon. All Rights Reserved.

using UnrealBuildTool;

public class DreamNarrativeStage : ModuleRules
{
	public DreamNarrativeStage(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		// The multi-track evaluator - camera, light, mask, vfx, anim, game. Restoring the world after
		// a performance is transactional: every track declares its undo, the director unwinds them in
		// reverse, and a watchdog forces the unwind when a performance is cut short.
		//
		// Stage attaches to story by line id and nothing else, so a project can enable one without the
		// other.
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
			"MovieSceneTracks",
			"CinematicCamera",
			"Niagara",
			"RenderCore",
			"AssetRegistry",
			// `move ... navigate`
			"AIModule",
			"NavigationSystem",
		});
	}
}
