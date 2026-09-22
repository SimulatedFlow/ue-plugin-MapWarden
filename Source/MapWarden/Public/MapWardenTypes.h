// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MapWardenTypes.generated.h"

/**
 * What kind of thing is wrong with the level.
 *
 * Seven kinds, each one switchable on its own and each one with its own threshold. That is not a feature
 * list, it is a survival requirement: a checker with one fixed opinion produces forty findings on its first
 * run against a real level, and a tool that is loud and unconfigurable on its first run gets switched off
 * in the first hour and never gets switched on again.
 */
UENUM(BlueprintType)
enum class EMapFindingKind : uint8
{
	/** Two actors of the same class at practically the same place. The Ctrl+D nobody moved. */
	Duplicate			UMETA(DisplayName = "Duplicate"),

	/** A static mesh actor set to No Collision whose mesh does bring a collision model with it. */
	MissingCollision	UMETA(DisplayName = "Missing Collision"),

	/** A light or mesh left on Movable that demonstrably never moves. */
	Mobility			UMETA(DisplayName = "Mobility"),

	/** An actor further from the centre of the level than the radius allows. */
	OutOfBounds			UMETA(DisplayName = "Out Of Bounds"),

	/** An overlap volume with nothing bound to it. Warning, never error - see EMapSeverity. */
	SilentTrigger		UMETA(DisplayName = "Silent Trigger"),

	/** A soft reference on a placed actor that no longer resolves. */
	DeadReference		UMETA(DisplayName = "Dead Reference"),

	/** Too many actors still carrying the default name their class gave them. Off by default. */
	DefaultName			UMETA(DisplayName = "Default Name"),
};

/** How bad a finding is. The verdict, and the process exit code behind the gate, are decided from these. */
UENUM(BlueprintType)
enum class EMapSeverity : uint8
{
	/** Worth knowing, changes nothing. Never fails a gate. */
	Info		UMETA(DisplayName = "Info"),

	/** Probably wrong. Fails the gate with 1. */
	Warning		UMETA(DisplayName = "Warning"),

	/** Wrong. Fails the gate with 2. */
	Error		UMETA(DisplayName = "Error"),
};

/** The three answers the gate can give, and the three numbers it exits with. */
UENUM(BlueprintType)
enum class EMapVerdict : uint8
{
	/** Nothing above Info. Exit code 0. */
	Ok		UMETA(DisplayName = "Ok"),

	/** At least one Warning and no Error. Exit code 1. */
	Warn	UMETA(DisplayName = "Warn"),

	/** At least one Error. Exit code 2. */
	Fail	UMETA(DisplayName = "Fail"),
};

/**
 * One soft reference that points at something which is not there any more.
 *
 * The property name travels with the path because "BP_Door_2 has a dead reference" is a sentence somebody
 * has to go hunting after, and "BP_Door_2.LinkedSwitch points at /Game/Old/BP_Switch, which no longer
 * exists" is a fix.
 */
USTRUCT(BlueprintType)
struct MAPWARDEN_API FMapWardenDeadReference
{
	GENERATED_BODY()

	/** The property that holds the path, qualified with the component name when it is on a component. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden")
	FName PropertyName;

	/** The path that does not resolve. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden")
	FString TargetPath;
};

/**
 * Everything the rules ever get to see about one placed actor.
 *
 * This is the single most important design decision in the plugin. The rules do not take an AActor. They
 * take this - names, a transform, a handful of booleans - which means AreDuplicates, IsOutOfBounds, Judge
 * and the whole of Analyze are covered by automation tests that build a level in four lines and never stand
 * a world up. It is also what makes the duplicate grid testable against a naive comparison over five
 * thousand actors, which is test six.
 *
 * The scanner fills these in from the world; nothing else in the plugin reads an actor.
 */
USTRUCT(BlueprintType)
struct MAPWARDEN_API FMapWardenActorInfo
{
	GENERATED_BODY()

	/** The actor's object name, e.g. StaticMeshActor_231. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden")
	FName Name;

	/** The label shown in the outliner, in the editor. Equal to Name in a cooked build. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden")
	FString Label;

	/** Full object path, so the editor module can find the actor again and select it. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden")
	FString Path;

	/** The actor's own class name, without the A/U prefix. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden")
	FName ClassName;

	/**
	 * The class and every parent class up to AActor, nearest first.
	 *
	 * Carried in full because the exemption list is matched against all of it: writing "StaticMeshActor" in
	 * the exempt list is a sentence about that class, but writing "MyBaseProp" has to also exempt the twelve
	 * Blueprints derived from it or the list is useless in a real project.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden")
	TArray<FName> ClassChain;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden")
	FVector Location = FVector::ZeroVector;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden")
	FRotator Rotation = FRotator::ZeroRotator;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden")
	FVector Scale = FVector::OneVector;

	/** The outliner folder the actor sits in, e.g. "Set Dressing/Props". Empty when it sits at the root. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden")
	FString FolderPath;

	/** The actor's tags, which is the third of the three exemption lists. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden")
	TArray<FName> Tags;

	//~ Collision -----------------------------------------------------------------------------------------

	/** True when the actor has at least one static mesh component with a mesh set. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden")
	bool bHasStaticMesh = false;

	/** The mesh the collision finding is about, for the sentence. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden")
	FName MeshName;

	/**
	 * True when that mesh has simple collision primitives on its body setup.
	 *
	 * This is the whole of check two. A mesh with no collision model is not a finding in either direction:
	 * somebody built that mesh without collision on purpose and the person who dragged it into the level did
	 * not make that decision. A mesh that has collision, placed with collision switched off, is almost
	 * always somebody clicking the wrong dropdown.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden")
	bool bMeshHasCollisionModel = false;

	/** True when that same component is set to No Collision. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden")
	bool bCollisionDisabled = false;

	//~ Mobility ------------------------------------------------------------------------------------------

	/** True when the actor has a light component. Movable lights are the expensive half of check three. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden")
	bool bHasLight = false;

	/** True when at least one scene component on the actor is set to Movable. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden")
	bool bMovable = false;

	/** A movement component is proof that something intends to move this. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden")
	bool bHasMovementComponent = false;

	/** A simulating body moves whether anybody intended it or not. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden")
	bool bSimulatesPhysics = false;

	/** Attached to something that is itself movable: it moves when its parent does. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden")
	bool bAttachedToMovable = false;

	/**
	 * Referenced by a level sequence actor standing in this level.
	 *
	 * As close to "has a Sequencer track" as this plugin can honestly get without taking a dependency on
	 * MovieScene, and the documentation says so in exactly those words. A sequence that possesses the actor
	 * through a binding stored in the sequence asset rather than through a reference on the actor in the
	 * level is not visible here, which is one of the reasons this finding is a warning and the exemption
	 * list exists.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden")
	bool bReferencedBySequence = false;

	//~ Triggers ------------------------------------------------------------------------------------------

	/** True when the actor has a primitive component that generates overlap events. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden")
	bool bIsOverlapTrigger = false;

	/** True when something is bound to the actor's or a component's begin/end overlap delegates. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden")
	bool bHasOverlapBinding = false;

	/** True when the level blueprint holds a reference to this actor. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden")
	bool bReferencedByLevelBlueprint = false;

	//~ The rest ------------------------------------------------------------------------------------------

	/** Soft references on this actor, or on its components, that do not resolve any more. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden")
	TArray<FMapWardenDeadReference> DeadReferences;

	/** True when the label is still the one the class handed out, e.g. StaticMeshActor_231. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden")
	bool bDefaultName = false;

	/** Which level the actor came from. With World Partition, this is the cell. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden")
	FName LevelName;
};

/**
 * One thing that is wrong, or one thing that was deliberately allowed to be wrong.
 *
 * Every finding names the actor, carries what was measured and what the threshold was, and ends in one
 * sentence about what to do. "6 duplicate actors" is a number nobody can act on. "SM_Crate_2 is 0.0 cm from
 * SM_Crate_1, same rotation and scale (tolerance 1.0 cm)" is a fix.
 */
USTRUCT(BlueprintType)
struct MAPWARDEN_API FMapWardenFinding
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden")
	EMapFindingKind Kind = EMapFindingKind::Duplicate;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden")
	EMapSeverity Severity = EMapSeverity::Warning;

	/** The actor this is about, by label where there is one. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden")
	FName ActorName;

	/** Full object path, which is what FocusFinding hands to the editor to select it. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden")
	FString ActorPath;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden")
	FName ActorClass;

	/** Where it is standing. Both for the report and for the camera the editor module moves. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden")
	FVector Location = FVector::ZeroVector;

	/** For a duplicate: the twin. For a dead reference: the path that does not resolve. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden")
	FName OtherName;

	/** Free-form second half of the subject: the mesh name, the property name, the broken path. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden")
	FString Context;

	/** What was measured: centimetres of separation, centimetres from the centre, a count. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden")
	float MeasuredValue = 0.0f;

	/** What it was measured against. Carried so the report can never claim a threshold it did not use. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden")
	float Threshold = 0.0f;

	/** The unit the two numbers above are in - "cm", "actors" - or empty when they mean nothing. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden")
	FString Unit;

	/**
	 * True when one of the three exemption lists took the teeth out of this finding.
	 *
	 * An excluded finding is not skipped. It is made, it is forced down to Info, it is counted as
	 * "excluded by settings" and it is still printed. An exemption list that could hide its own effect
	 * would be a way to make a level green by editing a settings page, which is the exact opposite of what
	 * a gate is for.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden")
	bool bExcluded = false;

	/** One sentence saying what to do about it. Filled in by UMapWardenStatics::Explain. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden")
	FString Detail;
};

/**
 * The settings, flattened, so the rules never read a UDeveloperSettings.
 *
 * A test can build one of these in three lines and turn exactly one check on. That is the point, and it is
 * also what makes "an exempt actor produces no error but is still counted" - test three - a four-line test
 * rather than a fixture.
 */
USTRUCT(BlueprintType)
struct MAPWARDEN_API FMapWardenRules
{
	GENERATED_BODY()

	//~ Which checks run ----------------------------------------------------------------------------------

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden|Checks")
	bool bCheckDuplicates = true;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden|Checks")
	bool bCheckMissingCollision = true;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden|Checks")
	bool bCheckMobility = true;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden|Checks")
	bool bCheckOutOfBounds = true;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden|Checks")
	bool bCheckSilentTriggers = true;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden|Checks")
	bool bCheckDeadReferences = true;

	/** Off by default. How many StaticMeshActor_231 you can live with is taste, and taste is not a gate. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden|Checks")
	bool bCheckDefaultNames = false;

	//~ Thresholds ----------------------------------------------------------------------------------------

	/** Two actors of one class closer than this, in centimetres, are candidates for being the same actor twice. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden|Thresholds")
	float DuplicateToleranceCm = 1.0f;

	/** ...and no further apart than this in any axis of rotation, in degrees. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden|Thresholds")
	float DuplicateRotationToleranceDeg = 1.0f;

	/** ...and no further apart than this in scale, as a fraction. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden|Thresholds")
	float DuplicateScaleTolerance = 0.01f;

	/** How far an actor may be from the centre of everything that was checked, in centimetres. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden|Thresholds")
	float OutOfBoundsRadiusCm = 100000.0f;

	/** How many default-named actors are allowed before the level gets a note about it. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden|Thresholds")
	int32 DefaultNameLimit = 50;

	//~ Severities ----------------------------------------------------------------------------------------

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden|Severity")
	EMapSeverity DuplicateSeverity = EMapSeverity::Warning;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden|Severity")
	EMapSeverity MissingCollisionSeverity = EMapSeverity::Warning;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden|Severity")
	EMapSeverity MobilitySeverity = EMapSeverity::Warning;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden|Severity")
	EMapSeverity OutOfBoundsSeverity = EMapSeverity::Error;

	/**
	 * The silent trigger.
	 *
	 * Whatever this is set to, the rule clamps it to Warning. A binding can be made at runtime, from a
	 * Blueprint that has not been loaded yet, and a check that cannot see that is a check that must not be
	 * allowed to fail a build over it. The line on the report says the same thing in words.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden|Severity")
	EMapSeverity SilentTriggerSeverity = EMapSeverity::Warning;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden|Severity")
	EMapSeverity DeadReferenceSeverity = EMapSeverity::Error;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden|Severity")
	EMapSeverity DefaultNameSeverity = EMapSeverity::Info;

	//~ The three exemption lists -------------------------------------------------------------------------

	/** Master switch, so a tester can see the same level both ways without editing a config file. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden|Exemptions")
	bool bUseExemptions = true;

	/** Class names. Matched against the whole class chain, so a base class exempts everything derived from it. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden|Exemptions")
	TArray<FName> ExemptClasses;

	/** Outliner folders. Matched as a prefix, so "Debug" also covers "Debug/Volumes". */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden|Exemptions")
	TArray<FString> ExemptFolders;

	/** Actor tags. The one exemption a level designer can apply without leaving the level. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden|Exemptions")
	TArray<FName> ExemptTags;

	//~ What gets looked at at all ------------------------------------------------------------------------

	/**
	 * Only actors that came with the level, and not the ones the game spawned while running.
	 *
	 * A game mode, a player controller and a HUD all sit at the origin with an identity transform, and none
	 * of them is anybody's level-building mistake. Skipped actors are counted and printed, so the difference
	 * between "checked 4,812" and "checked 4,812 of 6,004" never disappears.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden|Scanning")
	bool bOnlyPlacedActors = true;

	/**
	 * Classes that are never checked at all - as opposed to exempted, which is checked and then forgiven.
	 *
	 * Engine bookkeeping that lives in every level and belongs to nobody. Not findings, not exemptions; not
	 * the subject.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden|Scanning")
	TArray<FName> IgnoredClasses;

	/** Load the package behind a soft reference to prove it is dead, rather than only asking whether it exists. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden|Scanning")
	bool bResolveSoftReferences = false;
};

/**
 * What the scan looked at, as opposed to what it found.
 *
 * This exists so that "clean" can never be read as "nobody looked". With World Partition, only the cells in
 * memory exist, and a report that says nothing about that would be a report that quietly certifies a level
 * it never saw.
 */
USTRUCT(BlueprintType)
struct MAPWARDEN_API FMapWardenScanContext
{
	GENERATED_BODY()

	/** The level that was checked, by name. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden")
	FString LevelName;

	/** Every level that was in memory when the scan ran. With World Partition, these are the loaded cells. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden")
	TArray<FString> LoadedLevels;

	/** True when the world is partitioned, which is the whole reason for the honesty line on the report. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden")
	bool bWorldPartition = false;

	/**
	 * How many actors were passed over before the checks even started.
	 *
	 * Runtime-spawned actors, editor-only actors and the classes in the ignore list. Counted and printed,
	 * because the difference between "checked 4,812" and "checked 4,812 of 6,004" is the difference between
	 * a report and a claim.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden")
	int32 ActorsSkipped = 0;

	/** Where the world came from: "editor world", "play in editor", "game". */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden")
	FString SourceName;
};

/** The whole answer: the findings, the counts behind the header line, what was looked at, and the verdict. */
USTRUCT(BlueprintType)
struct MAPWARDEN_API FMapWardenReport
{
	GENERATED_BODY()

	/** Errors first, then warnings, then info; stable within a severity, so a screenshot means one thing. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden")
	TArray<FMapWardenFinding> Findings;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden")
	FString LevelName;

	/** How many actors the checks actually ran over. The first number on the report, always. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden")
	int32 ActorsChecked = 0;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden")
	int32 ActorsSkipped = 0;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden")
	int32 ErrorCount = 0;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden")
	int32 WarningCount = 0;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden")
	int32 InfoCount = 0;

	/** How many findings the exemption lists took the teeth out of. Always shown, never hidden. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden")
	int32 ExcludedCount = 0;

	/** Measured, wall clock, over the whole scan: gathering the actors and running the rules. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden")
	float ScanMilliseconds = 0.0f;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden")
	EMapVerdict Verdict = EMapVerdict::Ok;

	/** The centre everything was measured from, and the radius it was measured against. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden")
	FVector Centre = FVector::ZeroVector;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden")
	float BoundsRadiusCm = 0.0f;

	/** True when the world is partitioned and unloaded cells were therefore not checked. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden")
	bool bWorldPartition = false;

	/** The levels that were in memory. With World Partition, the cells that were checked. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden")
	TArray<FString> LoadedLevels;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden")
	FString SourceName;

	/**
	 * The checks that ran, and the checks that did not, in words.
	 *
	 * This is what stops a green report from being mistaken for a report that never ran - the failure mode
	 * of every quiet checker ever written. When MapWarden has nothing to say, it says what it looked for.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden")
	TArray<FString> ChecksRun;

	/** True once a scan has actually run. A default-constructed report is not a clean report. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MapWarden")
	bool bHasRun = false;
};
