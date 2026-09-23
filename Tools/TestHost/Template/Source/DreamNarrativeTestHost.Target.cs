// Copyright DreamMoon. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

/*
 * The game target of the DreamNarrative test host.
 *
 * Nothing builds this one: the runner only ever builds DreamNarrativeTestHostEditor, and the smoke
 * layer's standalone game runs that editor binary with -game. It is here so the host is an ordinary,
 * complete project.
 *
 * Building it is expensive: a game target against a source engine is monolithic and defaults to a
 * unique build environment, which compiles the whole engine again into this project.
 *
 * The settings are DevTest's (DevTest.Target.cs), so the host builds under exactly the rules the
 * project it stands in for builds under.
 */
public class DreamNarrativeTestHostTarget : TargetRules
{
	public DreamNarrativeTestHostTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("DreamNarrativeTestHost");
	}
}
