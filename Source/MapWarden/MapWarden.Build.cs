// Copyright 2026 Silvan Teufel. All Rights Reserved.

using UnrealBuildTool;

public class MapWarden : ModuleRules
{
	public MapWarden(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		// Everything that decides anything lives here, in a Runtime module, and that is the architecture of
		// this plugin in one sentence.
		//
		// The checks and the report are runtime code so that MapWarden.Gate runs in a commandlet with no
		// editor, and so that the panel is still there in the cooked build - which is the build where a level
		// full of duplicated actors actually costs you something. The editor module next door adds two
		// things and only two: the entry under Tools, and "select that actor in the viewport". The dependency
		// arrow points editor -> runtime and must never point back.
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",

			// AActor, TActorIterator, UWorldSubsystem, AHUD, UCanvas, UStaticMesh, ULightComponent - the
			// entire subject matter of this plugin is the engine's own scene representation.
			"Engine",

			// UMapWardenSettings is a UDeveloperSettings, so every switch, every threshold and all three
			// exemption lists appear under Project Settings > Plugins > MapWarden with no editor module in
			// the picture.
			"DeveloperSettings",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			// GWhiteTexture - the one-pixel texture the report panel is tiled from.
			"RenderCore",

			// Saved/MapWarden/report.json. Hand-rolled string building would have saved a dependency and
			// cost correct escaping, and the whole value of a gate is that a build server can read the
			// result without guessing.
			"Json",
			"JsonUtilities",
		});

		// Deliberately NOT here:
		//   UMG      - the report is drawn on UCanvas so it survives a cooked Shipping build. The demo panel
		//              is a UMG asset in Content that calls the Blueprint library, exactly as a project
		//              would.
		//   UnrealEd - see above. The editor module depends on this one; nothing here knows the editor
		//              exists. The one thing the runtime wants from the editor - "select this actor" - is
		//              asked for through a delegate the editor module fills in (FMapWardenFocus).
	}
}
