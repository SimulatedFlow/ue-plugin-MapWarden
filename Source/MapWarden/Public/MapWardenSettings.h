// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "MapWardenTypes.h"
#include "MapWardenSettings.generated.h"

/**
 * Project-wide settings for MapWarden, under Project Settings -> Plugins -> MapWarden.
 *
 * Three groups of things carry the weight here.
 *
 * Every check has its own switch. Not one master switch and a severity - a switch. A team that cares about
 * duplicate actors and has decided that Movable lights are their business and nobody else's must be able to
 * say exactly that, or they will turn the whole plugin off instead.
 *
 * Every check has its own threshold, in the unit a person thinks in: centimetres of separation, degrees of
 * rotation, centimetres from the centre of the level. The report prints the threshold next to the measured
 * value, so a finding can always be argued with.
 *
 * And there are three exemption lists, by class, by outliner folder and by actor tag, because MapWarden
 * cannot know what you meant. A crate with no collision may be decoration behind glass. What the lists
 * exempt is still counted and still shown as "excluded by settings" - see FMapWardenFinding::bExcluded for
 * why that is not negotiable.
 */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "MapWarden"))
class MAPWARDEN_API UMapWardenSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UMapWardenSettings();

	//~ UDeveloperSettings interface
	virtual FName GetCategoryName() const override;
	virtual FName GetSectionName() const override;

	/** The settings object, never null. */
	static const UMapWardenSettings& Get();

	/** The settings flattened into the struct the rules take. */
	FMapWardenRules MakeRules() const;

	//~ Which checks run ----------------------------------------------------------------------------------

	/** Two actors of the same class at practically the same place. The Ctrl+D nobody moved. */
	UPROPERTY(config, EditAnywhere, Category = "Checks")
	bool bCheckDuplicates = true;

	/**
	 * A static mesh actor set to No Collision whose mesh does bring a collision model with it.
	 *
	 * The reverse is deliberately not checked. A mesh with no collision model at all is a decision the
	 * person who made the mesh took, and reporting it would be MapWarden second-guessing an art department
	 * from inside a level editor.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Checks")
	bool bCheckMissingCollision = true;

	/** Lights and meshes on Movable that demonstrably never move. Shadow work paid for and never used. */
	UPROPERTY(config, EditAnywhere, Category = "Checks")
	bool bCheckMobility = true;

	/** The cube somebody lost while dragging: an actor far outside where the level actually is. */
	UPROPERTY(config, EditAnywhere, Category = "Checks")
	bool bCheckOutOfBounds = true;

	/** Overlap volumes with nothing bound to them. Always a warning; see SilentTriggerSeverity. */
	UPROPERTY(config, EditAnywhere, Category = "Checks")
	bool bCheckSilentTriggers = true;

	/** Soft references on placed actors that no longer resolve to anything. */
	UPROPERTY(config, EditAnywhere, Category = "Checks")
	bool bCheckDeadReferences = true;

	/**
	 * Too many actors still called what their class called them.
	 *
	 * Off by default, and that default is a decision rather than an oversight. How many StaticMeshActor_231
	 * a level may contain is taste, and taste does not belong in a gate unless somebody deliberately puts it
	 * there.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Checks")
	bool bCheckDefaultNames = false;

	//~ Thresholds ----------------------------------------------------------------------------------------

	/**
	 * How close two actors of one class have to be to count as the same actor twice, in centimetres.
	 *
	 * One centimetre by default, and it is also the cell size of the grid the search runs on, so raising it
	 * a great deal makes the search coarser rather than slower. Rotation and scale have to match as well -
	 * two crates a millimetre apart and turned ninety degrees are a wall, not an accident.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Thresholds", meta = (ClampMin = "0.001", UIMax = "100.0", Units = "cm"))
	float DuplicateToleranceCm = 1.0f;

	/** How far the two rotations may differ, per axis, in degrees. */
	UPROPERTY(config, EditAnywhere, Category = "Thresholds", meta = (ClampMin = "0.0", UIMax = "45.0"))
	float DuplicateRotationToleranceDeg = 1.0f;

	/** How far the two scales may differ, per axis, as a fraction. 0.01 is one per cent. */
	UPROPERTY(config, EditAnywhere, Category = "Thresholds", meta = (ClampMin = "0.0", UIMax = "1.0"))
	float DuplicateScaleTolerance = 0.01f;

	/**
	 * How far an actor may be from the centre of everything that was checked, in centimetres.
	 *
	 * A kilometre by default. The centre is the average position of every actor the scan looked at, not the
	 * origin, because a level built two kilometres from the origin is a normal thing and a checker that
	 * flagged all of it would be useless.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Thresholds", meta = (ClampMin = "1.0", Units = "cm"))
	float OutOfBoundsRadiusCm = 100000.0f;

	/** How many default-named actors are allowed before the level gets a note about it. */
	UPROPERTY(config, EditAnywhere, Category = "Thresholds", meta = (ClampMin = "1"))
	int32 DefaultNameLimit = 50;

	//~ Severities ----------------------------------------------------------------------------------------

	/** A duplicate. Warning by default: it costs memory and draw calls, it does not break the level. */
	UPROPERTY(config, EditAnywhere, Category = "Severity")
	EMapSeverity DuplicateSeverity = EMapSeverity::Warning;

	/** Collision switched off on a mesh that has collision. Warning: it is usually, not always, a mistake. */
	UPROPERTY(config, EditAnywhere, Category = "Severity")
	EMapSeverity MissingCollisionSeverity = EMapSeverity::Warning;

	/** A Movable light or mesh that never moves. Warning. */
	UPROPERTY(config, EditAnywhere, Category = "Severity")
	EMapSeverity MobilitySeverity = EMapSeverity::Warning;

	/** An actor outside the radius. Error by default: it is almost never intentional and it is free to fix. */
	UPROPERTY(config, EditAnywhere, Category = "Severity")
	EMapSeverity OutOfBoundsSeverity = EMapSeverity::Error;

	/**
	 * A trigger nothing listens to.
	 *
	 * Clamped to Warning by the rule whatever you set here, and that clamp is not a bug. Overlap delegates
	 * can be bound at runtime by code this scan never sees, so this finding is a suspicion, and a suspicion
	 * must not be allowed to fail somebody's build.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Severity")
	EMapSeverity SilentTriggerSeverity = EMapSeverity::Warning;

	/** A reference that no longer resolves. Error: it is provable, and it will be a null at runtime. */
	UPROPERTY(config, EditAnywhere, Category = "Severity")
	EMapSeverity DeadReferenceSeverity = EMapSeverity::Error;

	/** Default names. Info, and the check is off by default anyway. */
	UPROPERTY(config, EditAnywhere, Category = "Severity")
	EMapSeverity DefaultNameSeverity = EMapSeverity::Info;

	//~ Exemptions ----------------------------------------------------------------------------------------

	/**
	 * Use the exemption lists at all.
	 *
	 * Turning it off is how you find out what the lists are actually hiding, which is worth doing once a
	 * milestone. MapWarden.Exempt 0 does the same thing for one session without touching a config file.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Exemptions")
	bool bUseExemptions = true;

	/**
	 * Classes that are allowed to break the rules, by class name.
	 *
	 * Matched against the actor's whole class chain, so naming a base class exempts every Blueprint derived
	 * from it. Write the name without the A prefix: StaticMeshActor, not AStaticMeshActor - although both
	 * are accepted, because half the people typing this have the C++ name in their head.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Exemptions")
	TArray<FName> ExemptClasses;

	/**
	 * Outliner folders that are allowed to break the rules.
	 *
	 * Matched as a prefix, so "Debug" also covers "Debug/Volumes". This is the list a level designer reaches
	 * for: put the greybox in a folder and it stops shouting.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Exemptions")
	TArray<FString> ExemptFolders;

	/** Actor tags that are allowed to break the rules. The exemption you can apply without leaving the level. */
	UPROPERTY(config, EditAnywhere, Category = "Exemptions")
	TArray<FName> ExemptTags;

	//~ What gets looked at -------------------------------------------------------------------------------

	/**
	 * Check only the actors that came with the level, and not the ones the game spawned while running.
	 *
	 * On by default. A game mode, a player controller and a HUD all sit at the origin with an identity
	 * transform, and none of them is anybody's level-building mistake. In the editor every actor is a placed
	 * actor and this setting changes nothing; in a running game it is the difference between a report about
	 * a level and a report about a session.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Scanning")
	bool bOnlyPlacedActors = true;

	/**
	 * Classes that are never checked at all, by class name - as opposed to exempted, which is checked and
	 * then forgiven.
	 *
	 * The defaults are engine bookkeeping that lives in every level and belongs to nobody: the world
	 * settings, the builder brush, the level script actor. They are not findings and they are not
	 * exemptions; they are not the subject.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Scanning")
	TArray<FName> IgnoredClasses;

	/**
	 * Load the package behind a soft reference to prove it is dead.
	 *
	 * Off by default, and the default matters: with it off, MapWarden asks whether the package the path
	 * points at exists, which is fast and answers the case that actually happens - somebody deleted the
	 * asset. With it on, it also loads the package to check the object inside it is still there, which
	 * catches a renamed object inside a surviving package and costs you disk on every scan.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Scanning")
	bool bResolveSoftReferences = false;

	//~ The report ----------------------------------------------------------------------------------------

	/** Draw the report from the first frame. MapWarden.Show and MapWarden.Hide flip it. */
	UPROPERTY(config, EditAnywhere, Category = "Report")
	bool bShowReportByDefault = true;

	/** Run a check automatically once the world has begun play. */
	UPROPERTY(config, EditAnywhere, Category = "Report")
	bool bScanOnBeginPlay = true;

	/**
	 * How long after begin play the automatic check waits.
	 *
	 * Not laziness: actors bind their overlap delegates in BeginPlay, and a scan in the same frame as the
	 * map load would report every trigger in the level as silent.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Report", meta = (ClampMin = "0.0", UIMax = "10.0", Units = "Seconds"))
	float AutoScanDelaySeconds = 1.0f;

	/**
	 * Draw the report even when the project's HUD is not an AMapWardenHUD.
	 *
	 * A project with its own HUD class does not have to reparent it: turn this on and the same panel is
	 * drawn through AHUD::OnHUDPostRender instead. The two paths know about each other and cannot stack.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Report")
	bool bAutoDrawOnAnyHUD = false;

	/** How many findings the on-screen panel lists before it says how many more there are. */
	UPROPERTY(config, EditAnywhere, Category = "Report", meta = (ClampMin = "1", UIMax = "40"))
	int32 MaxReportRows = 14;

	/** Where MapWarden.Report and MapWarden.Gate write, relative to the project directory. */
	UPROPERTY(config, EditAnywhere, Category = "Report")
	FString ReportPath = TEXT("Saved/MapWarden/report.json");
};
