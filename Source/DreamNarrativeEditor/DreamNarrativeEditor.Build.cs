// Copyright DreamMoon. All Rights Reserved.

using UnrealBuildTool;

public class DreamNarrativeEditor : ModuleRules
{
	public DreamNarrativeEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		// Story graph view, stage timeline and the runtime debug panel. Every edit made here is
		// applied to the text source through the compiler's printer - this module never writes an
		// asset directly, and never holds narrative state of its own.
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"GameplayTags",
			"DreamNarrativeCore",
			"DreamNarrativeStory",
			"DreamNarrativeStage",
			"DreamNarrativeCompiler",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"UnrealEd",
			"Slate",
			"SlateCore",
			"InputCore",
			"EditorFramework",
			"EditorSubsystem",
			"ToolMenus",
			"AssetRegistry",
			"AssetTools",
			"GraphEditor",
			"ApplicationCore",
			"Projects",
			"WorkspaceMenuStructure",
			"GameplayTagsEditor",
			"DeveloperSettings",
			"LevelSequence",
			"MovieScene",
			"MovieSceneTracks",
		});
	}
}
