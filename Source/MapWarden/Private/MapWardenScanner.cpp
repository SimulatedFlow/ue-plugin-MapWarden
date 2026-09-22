// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "MapWardenScanner.h"

#include "Components/LightComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/Brush.h"
#include "Engine/Engine.h"
#include "Engine/Level.h"
#include "Engine/LevelScriptActor.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameFramework/MovementComponent.h"
#include "HAL/PlatformTime.h"
#include "MapWardenLog.h"
#include "MapWardenSettings.h"
#include "MapWardenStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "PhysicsEngine/BodySetup.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"

namespace MapWardenScan
{
	/** A class name with the Blueprint "_C" suffix taken off, which is the name a person would type. */
	static FName FriendlyClassName(const UClass* Class)
	{
		if (Class == nullptr)
		{
			return NAME_None;
		}

		FString Name = Class->GetName();
		if (Name.EndsWith(TEXT("_C"), ESearchCase::CaseSensitive))
		{
			Name.LeftChopInline(2);
		}

		return FName(*Name);
	}

	/** The class and every parent up to AActor, nearest first, in the form the exemption list is written in. */
	static void BuildClassChain(const UClass* Class, TArray<FName>& Out)
	{
		for (const UClass* Current = Class; Current != nullptr; Current = Current->GetSuperClass())
		{
			Out.Add(FriendlyClassName(Current));

			if (Current->GetFName() == TEXT("Actor"))
			{
				break;
			}
		}
	}

	/** True when this actor, or anything it inherits from, is on the ignore list. */
	static bool IsIgnoredClass(const TArray<FName>& Chain, const TArray<FName>& Ignored)
	{
		for (const FName Name : Ignored)
		{
			if (!Name.IsNone() && Chain.Contains(Name))
			{
				return true;
			}
		}

		return false;
	}

	/** True when the label is still the one the class handed out, e.g. StaticMeshActor_231. */
	static bool LooksLikeDefaultName(const FString& Label, const FName ClassName)
	{
		if (Label.IsEmpty() || ClassName.IsNone())
		{
			return false;
		}

		// Take the trailing _231 off, then ask whether what is left is just the class name. That covers both
		// StaticMeshActor_231 and the bare StaticMeshActor an actor gets when it is the first of its kind.
		FString Stem = Label;

		int32 Underscore = INDEX_NONE;
		if (Stem.FindLastChar(TEXT('_'), Underscore) && Underscore > 0)
		{
			const FString Suffix = Stem.RightChop(Underscore + 1);
			if (!Suffix.IsEmpty() && Suffix.IsNumeric())
			{
				Stem.LeftInline(Underscore);
			}
		}

		return Stem.Equals(ClassName.ToString(), ESearchCase::IgnoreCase);
	}

	/**
	 * Every actor this object refers to, one subobject deep.
	 *
	 * Used for two things and only two: which actors the level blueprint holds a reference to, and which
	 * actors a level sequence actor points at. Both are asked of a handful of objects per level, never of
	 * every actor, because FReferenceFinder is not cheap enough to run five thousand times.
	 *
	 * The extra hop through the object's own subobjects is what makes a level sequence actor's binding
	 * overrides visible - they live on a subobject, not on the actor.
	 */
	static void CollectReferencedActors(UObject* Source, TSet<const AActor*>& Out)
	{
		if (!IsValid(Source))
		{
			return;
		}

		TArray<UObject*> Referenced;
		FReferenceFinder Finder(Referenced);
		Finder.FindReferences(Source);

		TArray<UObject*> Nested;
		for (UObject* Object : Referenced)
		{
			if (IsValid(Object) && Object->IsIn(Source))
			{
				FReferenceFinder SubFinder(Nested);
				SubFinder.FindReferences(Object);
			}
		}

		Referenced.Append(Nested);

		for (UObject* Object : Referenced)
		{
			if (const AActor* Actor = Cast<AActor>(Object))
			{
				Out.Add(Actor);
			}
			else if (const UActorComponent* Component = Cast<UActorComponent>(Object))
			{
				// A reference to somebody's collision component is a reference to that actor. A level
				// blueprint that drags in a component and binds its overlap event is the single most common
				// shape of "this trigger is not silent after all".
				if (const AActor* Owner = Component->GetOwner())
				{
					Out.Add(Owner);
				}
			}
		}
	}

	/**
	 * Does this soft reference still point at something?
	 *
	 * Cheap first: an already-loaded object settles it. Then whether the package exists at all, which is the
	 * case that actually happens - somebody deleted the asset and the redirector went with it. Loading the
	 * package to check the object inside it is behind a setting and off by default, because doing it on
	 * every scan means paying disk for every soft reference in the level.
	 */
	static bool SoftReferenceResolves(const FSoftObjectPath& Path, const bool bAllowLoad)
	{
		if (Path.IsNull())
		{
			return true;
		}

		if (Path.ResolveObject() != nullptr)
		{
			return true;
		}

		const FString PackageName = Path.GetLongPackageName();
		if (PackageName.IsEmpty() || !FPackageName::IsValidLongPackageName(PackageName))
		{
			// /Script paths and anything malformed. Not this check's business: a class reference that does
			// not resolve is a missing module, not a broken level, and saying otherwise would be a checker
			// blaming a level designer for somebody else's build.
			return true;
		}

		if (!FPackageName::DoesPackageExist(PackageName))
		{
			return false;
		}

		return bAllowLoad ? (Path.TryLoad() != nullptr) : true;
	}

	/** Every soft reference on this object that does not resolve, named by the property that holds it. */
	static void CollectDeadReferences(
		const UObject* Object,
		const FString& Prefix,
		const bool bAllowLoad,
		TArray<FMapWardenDeadReference>& Out)
	{
		if (!IsValid(Object))
		{
			return;
		}

		// Recurses into structs and arrays, which is where half the soft references in a real Blueprint
		// actually live - a TArray<TSoftObjectPtr<>> of things to spawn, a struct with a level to open.
		for (TPropertyValueIterator<FSoftObjectProperty> It(Object->GetClass(), Object); It; ++It)
		{
			const FSoftObjectProperty* Property = It.Key();
			const void* Value = It->Value;
			if (Property == nullptr || Value == nullptr)
			{
				continue;
			}

			const FSoftObjectPtr& Pointer = *static_cast<const FSoftObjectPtr*>(Value);
			const FSoftObjectPath Path = Pointer.ToSoftObjectPath();

			if (SoftReferenceResolves(Path, bAllowLoad))
			{
				continue;
			}

			FMapWardenDeadReference Dead;
			Dead.PropertyName = FName(*(Prefix + Property->GetName()));
			Dead.TargetPath = Path.ToString();
			Out.Add(MoveTemp(Dead));
		}
	}
}

void FMapWardenScanner::DescribeActor(const AActor* Actor, const FMapWardenRules& Rules, FMapWardenActorInfo& Out)
{
	using namespace MapWardenScan;

	if (!IsValid(Actor))
	{
		return;
	}

	Out.Name = Actor->GetFName();
	Out.Path = Actor->GetPathName();
	Out.ClassName = FriendlyClassName(Actor->GetClass());
	BuildClassChain(Actor->GetClass(), Out.ClassChain);
	Out.Tags = Actor->Tags;

#if WITH_EDITOR
	Out.Label = Actor->GetActorLabel();
	Out.FolderPath = Actor->GetFolderPath().ToString();
#else
	// In a cooked build there are no labels and no outliner folders. The report falls back to object names,
	// which is what a packaged game has, and the folder exemption list simply matches nothing - said in the
	// documentation rather than discovered.
	Out.Label = Actor->GetName();
#endif

	Out.bDefaultName = LooksLikeDefaultName(Out.Label, Out.ClassName);

	const FTransform Transform = Actor->GetActorTransform();
	Out.Location = Transform.GetLocation();
	Out.Rotation = Transform.Rotator();
	Out.Scale = Transform.GetScale3D();

	if (const ULevel* Level = Actor->GetLevel())
	{
		Out.LevelName = FName(*FPackageName::GetShortName(Level->GetOutermost()->GetName()));
	}

	//~ Components ----------------------------------------------------------------------------------------

	Out.bHasMovementComponent = Actor->FindComponentByClass<UMovementComponent>() != nullptr;

	// The attachment question is asked of the parent actor rather than of this actor's own root, because
	// "it moves because the thing it is bolted to moves" is the whole point of the exception.
	if (const AActor* Parent = Actor->GetAttachParentActor())
	{
		const USceneComponent* ParentRoot = Parent->GetRootComponent();
		Out.bAttachedToMovable = ParentRoot != nullptr && ParentRoot->Mobility != EComponentMobility::Static;
	}

	for (const UActorComponent* Component : Actor->GetComponents())
	{
		if (!IsValid(Component))
		{
			continue;
		}

		if (const USceneComponent* Scene = Cast<USceneComponent>(Component))
		{
			Out.bMovable |= (Scene->Mobility == EComponentMobility::Movable);
		}

		if (Component->IsA<ULightComponent>())
		{
			Out.bHasLight = true;
		}

		if (const UStaticMeshComponent* Mesh = Cast<UStaticMeshComponent>(Component))
		{
			if (const UStaticMesh* StaticMesh = Mesh->GetStaticMesh())
			{
				Out.bHasStaticMesh = true;

				if (Out.MeshName.IsNone())
				{
					Out.MeshName = StaticMesh->GetFName();
				}

				// The collision question, in full. "Does this mesh bring a collision model with it" means
				// simple collision primitives on the body setup - the boxes and capsules somebody drew in
				// the mesh editor. That is the work this placement is throwing away.
				if (const UBodySetup* BodySetup = StaticMesh->GetBodySetup())
				{
					if (BodySetup->AggGeom.GetElementCount() > 0)
					{
						Out.bMeshHasCollisionModel = true;

						if (Mesh->GetCollisionEnabled() == ECollisionEnabled::NoCollision)
						{
							Out.bCollisionDisabled = true;
							Out.MeshName = StaticMesh->GetFName();
						}
					}
				}
			}
		}

		if (const UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component))
		{
			Out.bSimulatesPhysics |= Primitive->BodyInstance.bSimulatePhysics;

			const ECollisionEnabled::Type Collision = Primitive->GetCollisionEnabled();
			const bool bQueries = Collision == ECollisionEnabled::QueryOnly || Collision == ECollisionEnabled::QueryAndPhysics;

			if (bQueries && Primitive->GetGenerateOverlapEvents())
			{
				Out.bIsOverlapTrigger = true;
			}

			Out.bHasOverlapBinding |= Primitive->OnComponentBeginOverlap.IsBound();
			Out.bHasOverlapBinding |= Primitive->OnComponentEndOverlap.IsBound();
		}
	}

	// The actor-level delegates as well as the component ones. A Blueprint that adds an "On Actor Begin
	// Overlap" node binds here and not on the component, and missing that would make every such trigger
	// look silent.
	Out.bHasOverlapBinding |= Actor->OnActorBeginOverlap.IsBound();
	Out.bHasOverlapBinding |= Actor->OnActorEndOverlap.IsBound();

	//~ Dead references -----------------------------------------------------------------------------------

	if (Rules.bCheckDeadReferences)
	{
		CollectDeadReferences(Actor, FString(), Rules.bResolveSoftReferences, Out.DeadReferences);

		for (const UActorComponent* Component : Actor->GetComponents())
		{
			if (IsValid(Component))
			{
				CollectDeadReferences(Component, Component->GetName() + TEXT("."), Rules.bResolveSoftReferences, Out.DeadReferences);
			}
		}
	}
}

void FMapWardenScanner::GatherActors(
	const UWorld* World,
	const FMapWardenRules& Rules,
	TArray<FMapWardenActorInfo>& OutActors,
	FMapWardenScanContext& OutContext)
{
	using namespace MapWardenScan;

	OutActors.Reset();

	if (!IsValid(World))
	{
		return;
	}

	OutContext.LevelName = UWorld::RemovePIEPrefix(World->GetMapName());
	OutContext.bWorldPartition = World->IsPartitionedWorld();

	switch (World->WorldType)
	{
	case EWorldType::Editor:		OutContext.SourceName = TEXT("editor world"); break;
	case EWorldType::PIE:			OutContext.SourceName = TEXT("play in editor"); break;
	case EWorldType::Game:			OutContext.SourceName = TEXT("game"); break;
	default:						OutContext.SourceName = TEXT("other world"); break;
	}

	// Which levels were in memory. On a partitioned world these are the loaded cells, and this list is what
	// the coverage line on the report is built from - the difference between "the level is clean" and "the
	// part of the level that happened to be loaded is clean".
	TSet<const AActor*> LevelBlueprintReferences;
	TSet<const AActor*> SequenceReferences;

	for (const ULevel* Level : World->GetLevels())
	{
		if (!IsValid(Level))
		{
			continue;
		}

		OutContext.LoadedLevels.Add(FPackageName::GetShortName(Level->GetOutermost()->GetName()));

		if (ALevelScriptActor* ScriptActor = Level->GetLevelScriptActor())
		{
			CollectReferencedActors(ScriptActor, LevelBlueprintReferences);
		}
	}

	const ABrush* BuilderBrush = World->GetDefaultBrush();
	const bool bGameWorld = World->IsGameWorld();

	// One pass to find the level sequence actors, so that "is this Movable thing animated" has an answer
	// before the first mobility finding is made. There are single digits of these in a level; walking them
	// twice costs nothing and doing it in the main loop would mean the answer depended on iteration order.
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!IsValid(Actor) || Actor->IsTemplate())
		{
			continue;
		}

		if (Actor->GetClass()->GetName().Contains(TEXT("LevelSequenceActor")))
		{
			CollectReferencedActors(Actor, SequenceReferences);
		}
	}

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		const AActor* Actor = *It;

		// Not counted as skipped: these are not actors in any sense a level designer would recognise.
		if (!IsValid(Actor) || Actor->IsTemplate() || Actor == BuilderBrush)
		{
			continue;
		}

		if (Actor->IsEditorOnly())
		{
			++OutContext.ActorsSkipped;
			continue;
		}

		// In a running game, only what came with the level. The game mode, the player controller, the HUD
		// and the player state all sit at the origin with an identity transform and none of them is a
		// level-building mistake; without this line the duplicate check would report the framework.
		// In an editor world every actor is a placed actor, so this changes nothing there.
		if (bGameWorld && Rules.bOnlyPlacedActors && !Actor->IsNetStartupActor())
		{
			++OutContext.ActorsSkipped;
			continue;
		}

		FMapWardenActorInfo Info;
		DescribeActor(Actor, Rules, Info);

		if (IsIgnoredClass(Info.ClassChain, Rules.IgnoredClasses))
		{
			++OutContext.ActorsSkipped;
			continue;
		}

		Info.bReferencedByLevelBlueprint = LevelBlueprintReferences.Contains(Actor);
		Info.bReferencedBySequence = SequenceReferences.Contains(Actor);

		OutActors.Add(MoveTemp(Info));
	}
}

FMapWardenReport FMapWardenScanner::Run(const UWorld* World, const FMapWardenRules& Rules)
{
	const double Start = FPlatformTime::Seconds();

	TArray<FMapWardenActorInfo> Actors;
	FMapWardenScanContext Context;
	GatherActors(World, Rules, Actors, Context);

	FMapWardenReport Report = UMapWardenStatics::Analyze(Actors, Rules, Context);

	// Measured over the whole thing - walking the world as well as running the rules - because the number on
	// the report is meant to answer "can I afford to run this", and half a number cannot.
	Report.ScanMilliseconds = static_cast<float>((FPlatformTime::Seconds() - Start) * 1000.0);
	return Report;
}

FMapWardenReport FMapWardenScanner::RunWithProjectSettings(const UWorld* World)
{
	return Run(World, UMapWardenSettings::Get().MakeRules());
}

UWorld* FMapWardenScanner::FindBestWorld()
{
	if (GEngine == nullptr)
	{
		return nullptr;
	}

	UWorld* Editor = nullptr;

	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		UWorld* World = Context.World();
		if (!IsValid(World))
		{
			continue;
		}

		// A running game wins, then play-in-editor. The editor world is the fallback rather than the first
		// choice, and it is also the one a commandlet that loaded a map for the gate ends up with.
		if (Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE)
		{
			return World;
		}

		if (Context.WorldType == EWorldType::Editor && Editor == nullptr)
		{
			Editor = World;
		}
	}

	return Editor;
}

bool FMapWardenScanner::WriteReportFile(const FMapWardenReport& Report, const FString& Path, FString& OutFullPath)
{
	FString Target = Path.IsEmpty() ? UMapWardenSettings::Get().ReportPath : Path;
	if (Target.IsEmpty())
	{
		Target = TEXT("Saved/MapWarden/report.json");
	}

	OutFullPath = FPaths::IsRelative(Target)
		? FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / Target)
		: Target;

	if (!FFileHelper::SaveStringToFile(UMapWardenStatics::ReportToJson(Report), *OutFullPath))
	{
		UE_LOG(LogMapWarden, Error, TEXT("MapWarden: could not write the report to %s."), *OutFullPath);
		return false;
	}

	UE_LOG(LogMapWarden, Display, TEXT("MapWarden: report written to %s (verdict %s, exit code %d)."),
		*OutFullPath,
		*UMapWardenStatics::VerdictName(Report.Verdict),
		UMapWardenStatics::VerdictExitCode(Report.Verdict));
	return true;
}

void FMapWardenScanner::LogReport(const FMapWardenReport& Report, const bool bAllFindings)
{
	UE_LOG(LogMapWarden, Display, TEXT("%s"), *UMapWardenStatics::FormatHeadline(Report));
	UE_LOG(LogMapWarden, Display, TEXT("  %s"), *UMapWardenStatics::FormatCoverage(Report));

	if (Report.bWorldPartition)
	{
		// Said twice, on purpose: once in the coverage line and once on its own. On a partitioned world this
		// is the difference between a report and a false certificate.
		UE_LOG(LogMapWarden, Warning,
			TEXT("  World Partition: only the cells that were loaded were checked. Unloaded cells were NOT checked."));
	}

	if (bAllFindings)
	{
		for (const FMapWardenFinding& Finding : Report.Findings)
		{
			UE_LOG(LogMapWarden, Display, TEXT("  %s"), *UMapWardenStatics::FormatFinding(Finding));
			UE_LOG(LogMapWarden, Display, TEXT("      %s"), *Finding.Detail);
		}

		for (const FString& Check : Report.ChecksRun)
		{
			UE_LOG(LogMapWarden, Display, TEXT("  checked: %s"), *Check);
		}
	}
}
