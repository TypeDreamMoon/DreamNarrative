// Copyright DreamMoon. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

/*
 * The editor target of the DreamNarrative test host -- the one the runner builds and launches.
 *
 * The shared build environment is the point of this file. An editor target is modular and shares the
 * engine's build products: the engine modules already in Engine/Binaries/Win64 are linked against as
 * they are, and only this project's module and the DreamNarrative plugin (whose Binaries and
 * Intermediate live in its own worktree) are compiled. Shared is already the default for a modular
 * target; it is spelled out so nobody "fixes" it to Unique, which would build the engine a second time.
 *
 * Nothing marked [RequiresUniqueBuildEnvironment] may be set here: under a shared environment that is
 * a build error. The other settings are DevTest's (DevTestEditor.Target.cs) line for line, so the two
 * projects agree on every engine module being up to date and neither rebuilds the other's.
 */
public class DreamNarrativeTestHostEditorTarget : TargetRules
{
	public DreamNarrativeTestHostEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		BuildEnvironment = TargetBuildEnvironment.Shared;
		ExtraModuleNames.Add("DreamNarrativeTestHost");
	}
}
