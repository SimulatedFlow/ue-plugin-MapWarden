// Copyright 2026 Silvan Teufel. All Rights Reserved.

using UnrealBuildTool;

public class MapWardenEditor : ModuleRules
{
	public MapWardenEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		// Two jobs, and nothing else.
		//
		// One: the entry under Tools > MapWarden, so the same checks the build server runs can be run on the
		// level that is open right now, with no game standing.
		//
		// Two: "select that actor in the viewport". The runtime module has an actor path and no way to act
		// on it in an editor it is not allowed to know about, so it asks through FMapWardenFocus - a
		// delegate declared in the runtime module and filled in here.
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",

			// The checks, the report, the settings and the delegate this module fills in.
			"MapWarden",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			// GEditor: the editor world to check, and the selection to move.
			"UnrealEd",

			// The entry under Tools, and the toast that says how the check went.
			"ToolMenus",
			"Slate",
			"SlateCore",

			// The level editor's viewport clients, so the camera can be moved to the actor a finding is
			// about rather than only selecting it somewhere off screen.
			"LevelEditor",
		});
	}
}
