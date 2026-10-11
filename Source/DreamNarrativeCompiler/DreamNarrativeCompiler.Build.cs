// Copyright DreamMoon. All Rights Reserved.

using UnrealBuildTool;

public class DreamNarrativeCompiler : ModuleRules
{
	public DreamNarrativeCompiler(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		// Text source in, assets out: lexer, parser, validator, generator, plus the commandlet that
		// runs the lot headlessly for CI. The printer that writes an edited syntax tree back to source
		// without disturbing comments or formatting also lives here - the graphical editors are only
		// ever a projection, and this module is what keeps that true.
		//
		// UncookedOnly: a packaged game ships the generated assets, never the compiler.
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"GameplayTags",
			"DreamNarrativeCore",
			// Its stage compiler hands out the Stage module's data.
			"DreamNarrativeStage",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"DreamNarrativeStory",
			"AssetRegistry",
			"Json",
			"JsonUtilities",
			"Projects",
		});

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(new string[]
			{
				"UnrealEd",
				"DirectoryWatcher",
				// Translations go into a localization target's archive.
				"Localization",
			});
		}
	}
}
