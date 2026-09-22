// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "MapWardenStatics.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/PlatformTime.h"
#include "MapWardenFocus.h"
#include "MapWardenLog.h"
#include "MapWardenScanner.h"
#include "MapWardenSubsystem.h"
#include "Math/IntVector.h"
#include "Misc/StringBuilder.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace MapWardenRules
{
	/** Errors first, then warnings, then info. */
	static int32 SeverityRank(const EMapSeverity Severity)
	{
		switch (Severity)
		{
		case EMapSeverity::Error:	return 0;
		case EMapSeverity::Warning:	return 1;
		default:					return 2;
		}
	}

	/** A severity may be lowered by a rule but never raised above what the rule allows. */
	static EMapSeverity ClampTo(const EMapSeverity Severity, const EMapSeverity Ceiling)
	{
		return (SeverityRank(Severity) < SeverityRank(Ceiling)) ? Ceiling : Severity;
	}

	/** Which grid cell a position falls in, for a given cell size. */
	static FIntVector CellOf(const FVector& Position, const double CellSize)
	{
		// Clamped rather than trusted. A level with an actor at 1e30 - which happens, usually right after
		// somebody typed into the wrong transform field - would otherwise overflow the cell index and put
		// two unrelated actors in the same bucket. The duplicate test itself still has the final say, so a
		// clamped outlier costs one wasted comparison and nothing else.
		auto Axis = [CellSize](const double Value) -> int32
		{
			if (!FMath::IsFinite(Value))
			{
				return 0;
			}

			const double Cell = FMath::Floor(Value / CellSize);
			return static_cast<int32>(FMath::Clamp(Cell, -1.0e9, 1.0e9));
		};

		return FIntVector(Axis(Position.X), Axis(Position.Y), Axis(Position.Z));
	}

	/** True when every axis of the two rotators is within Tolerance degrees, the short way round. */
	static bool RotationsMatch(const FRotator& A, const FRotator& B, const float ToleranceDeg)
	{
		const FRotator Delta = (A - B).GetNormalized();
		return FMath::Abs(Delta.Pitch) <= ToleranceDeg
			&& FMath::Abs(Delta.Yaw) <= ToleranceDeg
			&& FMath::Abs(Delta.Roll) <= ToleranceDeg;
	}

	static bool ScalesMatch(const FVector& A, const FVector& B, const float Tolerance)
	{
		return FMath::Abs(A.X - B.X) <= Tolerance
			&& FMath::Abs(A.Y - B.Y) <= Tolerance
			&& FMath::Abs(A.Z - B.Z) <= Tolerance;
	}

	/** Fills in the sentence and applies the exemption lists. Every finding leaves through here. */
	static FMapWardenFinding Finish(FMapWardenFinding Finding, const FMapWardenActorInfo& Actor, const FMapWardenRules& Rules)
	{
		if (UMapWardenStatics::IsExempt(Actor, Rules))
		{
			// Made, not skipped. The finding still exists, still names the actor and is still printed - it
			// is forced down to Info and counted as excluded. An exemption list that could hide its own
			// effect would be a way to turn a level green from a settings page, which is the exact thing a
			// gate exists to prevent.
			Finding.bExcluded = true;
			Finding.Severity = EMapSeverity::Info;
		}

		Finding.Detail = UMapWardenStatics::Explain(Finding);
		return Finding;
	}

	/** The common half of every finding: which actor, where, of what class. */
	static FMapWardenFinding Begin(const FMapWardenActorInfo& Actor, const EMapFindingKind Kind, const EMapSeverity Severity)
	{
		FMapWardenFinding Finding;
		Finding.Kind = Kind;
		Finding.Severity = Severity;
		Finding.ActorName = FName(*(Actor.Label.IsEmpty() ? Actor.Name.ToString() : Actor.Label));
		Finding.ActorPath = Actor.Path;
		Finding.ActorClass = Actor.ClassName;
		Finding.Location = Actor.Location;
		return Finding;
	}
}

//~ The rules ---------------------------------------------------------------------------------------------

bool UMapWardenStatics::AreDuplicates(
	const FMapWardenActorInfo& A,
	const FMapWardenActorInfo& B,
	const float ToleranceCm,
	const float RotationToleranceDeg,
	const float ScaleTolerance)
{
	using namespace MapWardenRules;

	// Same class first, and not "similar" class. Two different props at the same spot is a decision - a
	// pillar inside an alcove, a decal on a wall. The same class twice with the same transform is not.
	if (A.ClassName != B.ClassName)
	{
		return false;
	}

	// Squared, so the common case - two actors that are nowhere near each other - costs no square root.
	const double ToleranceSq = static_cast<double>(ToleranceCm) * static_cast<double>(ToleranceCm);
	if (FVector::DistSquared(A.Location, B.Location) > ToleranceSq)
	{
		return false;
	}

	// Rotation and scale have to match too. Two crates a millimetre apart and turned ninety degrees are a
	// wall somebody built; two crates a millimetre apart facing the same way are a Ctrl+D nobody moved.
	// Without this, every tiled floor in the project would be reported.
	return RotationsMatch(A.Rotation, B.Rotation, RotationToleranceDeg)
		&& ScalesMatch(A.Scale, B.Scale, ScaleTolerance);
}

bool UMapWardenStatics::IsOutOfBounds(const FVector& Position, const FVector& Centre, const float RadiusCm)
{
	const double RadiusSq = static_cast<double>(RadiusCm) * static_cast<double>(RadiusCm);
	return FVector::DistSquared(Position, Centre) > RadiusSq;
}

EMapVerdict UMapWardenStatics::Judge(const TArray<FMapWardenFinding>& Findings)
{
	bool bAnyWarning = false;

	for (const FMapWardenFinding& Finding : Findings)
	{
		if (Finding.Severity == EMapSeverity::Error)
		{
			// One error is enough, and there is nothing that can outvote it. A gate whose answer depended on
			// how many errors there were would be a gate somebody could argue with.
			return EMapVerdict::Fail;
		}

		bAnyWarning |= (Finding.Severity == EMapSeverity::Warning);
	}

	return bAnyWarning ? EMapVerdict::Warn : EMapVerdict::Ok;
}

bool UMapWardenStatics::IsExempt(const FMapWardenActorInfo& Actor, const FMapWardenRules& Rules)
{
	if (!Rules.bUseExemptions)
	{
		return false;
	}

	// By class, matched against the whole chain, so naming a base class exempts every Blueprint derived from
	// it. Both spellings are accepted - StaticMeshActor and AStaticMeshActor - because half the people typing
	// this have the C++ name in their head and being pedantic about a leading A would only produce a list
	// that silently does nothing.
	for (const FName Exempt : Rules.ExemptClasses)
	{
		if (Exempt.IsNone())
		{
			continue;
		}

		const FString ExemptString = Exempt.ToString();
		const FString Bare = (ExemptString.Len() > 1 && (ExemptString[0] == TEXT('A') || ExemptString[0] == TEXT('U')))
			? ExemptString.RightChop(1)
			: ExemptString;

		for (const FName ClassName : Actor.ClassChain)
		{
			if (ClassName == Exempt || ClassName.ToString().Equals(Bare, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}
	}

	// By outliner folder, as a prefix, so "Debug" also covers "Debug/Volumes". This is the list a level
	// designer actually reaches for: drop the greybox in a folder and it stops shouting.
	if (!Actor.FolderPath.IsEmpty())
	{
		for (const FString& Folder : Rules.ExemptFolders)
		{
			if (Folder.IsEmpty())
			{
				continue;
			}

			if (Actor.FolderPath.Equals(Folder, ESearchCase::IgnoreCase)
				|| Actor.FolderPath.StartsWith(Folder + TEXT("/"), ESearchCase::IgnoreCase))
			{
				return true;
			}
		}
	}

	// By actor tag: the one exemption that can be applied without leaving the level.
	for (const FName Tag : Rules.ExemptTags)
	{
		if (!Tag.IsNone() && Actor.Tags.Contains(Tag))
		{
			return true;
		}
	}

	return false;
}

FVector UMapWardenStatics::ComputeCentre(const TArray<FMapWardenActorInfo>& Actors)
{
	if (Actors.Num() == 0)
	{
		return FVector::ZeroVector;
	}

	// The average, not the origin. A level built two kilometres from the origin is an ordinary thing, and a
	// bounds check measured from the origin would report all of it.
	FVector Sum = FVector::ZeroVector;
	for (const FMapWardenActorInfo& Actor : Actors)
	{
		Sum += Actor.Location;
	}

	return Sum / static_cast<double>(Actors.Num());
}

void UMapWardenStatics::FindDuplicatePairs(
	const TArray<FMapWardenActorInfo>& Actors,
	const FMapWardenRules& Rules,
	TArray<TPair<int32, int32>>& OutPairs)
{
	using namespace MapWardenRules;

	OutPairs.Reset();

	if (Actors.Num() < 2)
	{
		return;
	}

	//
	// This is the only part of MapWarden that would be naively quadratic, and it is worth being explicit
	// about why that matters. Twenty thousand actors compared against each other is two hundred million
	// distance tests - the difference between a check that runs in the time it takes to blink and one
	// nobody ever runs twice.
	//
	// So the actors go into a uniform grid whose cell size is the duplicate tolerance. Two actors closer
	// than the tolerance are, by construction, either in the same cell or in one of the twenty-six around
	// it, so each actor is only ever compared against its own neighbourhood and the whole search stays
	// linear in the number of actors. The cell size being the tolerance is what makes that true: a bigger
	// cell would mean more comparisons per actor, a smaller one would break the twenty-seven-cell guarantee.
	//
	// Test six checks this grid against the naive comparison over five thousand actors, because a fast
	// search that quietly misses a pair is worse than a slow one that does not.
	//
	const double CellSize = FMath::Max(static_cast<double>(Rules.DuplicateToleranceCm), 0.001);

	TMap<FIntVector, TArray<int32>> Grid;
	Grid.Reserve(Actors.Num());

	for (int32 Index = 0; Index < Actors.Num(); ++Index)
	{
		Grid.FindOrAdd(CellOf(Actors[Index].Location, CellSize)).Add(Index);
	}

	for (int32 Index = 0; Index < Actors.Num(); ++Index)
	{
		const FIntVector Base = CellOf(Actors[Index].Location, CellSize);

		for (int32 dX = -1; dX <= 1; ++dX)
		{
			for (int32 dY = -1; dY <= 1; ++dY)
			{
				for (int32 dZ = -1; dZ <= 1; ++dZ)
				{
					const TArray<int32>* Bucket = Grid.Find(FIntVector(Base.X + dX, Base.Y + dY, Base.Z + dZ));
					if (Bucket == nullptr)
					{
						continue;
					}

					for (const int32 Other : *Bucket)
					{
						// Each pair once, lower index first. Without this every duplicate would be found
						// twice - once from each end - and the report would say six where it means three.
						if (Other <= Index)
						{
							continue;
						}

						if (AreDuplicates(Actors[Index], Actors[Other],
							Rules.DuplicateToleranceCm, Rules.DuplicateRotationToleranceDeg, Rules.DuplicateScaleTolerance))
						{
							OutPairs.Emplace(Index, Other);
						}
					}
				}
			}
		}
	}
}

void UMapWardenStatics::FindDuplicatePairsNaive(
	const TArray<FMapWardenActorInfo>& Actors,
	const FMapWardenRules& Rules,
	TArray<TPair<int32, int32>>& OutPairs)
{
	OutPairs.Reset();

	for (int32 Index = 0; Index < Actors.Num(); ++Index)
	{
		for (int32 Other = Index + 1; Other < Actors.Num(); ++Other)
		{
			if (AreDuplicates(Actors[Index], Actors[Other],
				Rules.DuplicateToleranceCm, Rules.DuplicateRotationToleranceDeg, Rules.DuplicateScaleTolerance))
			{
				OutPairs.Emplace(Index, Other);
			}
		}
	}
}

FMapWardenReport UMapWardenStatics::Analyze(
	const TArray<FMapWardenActorInfo>& Actors,
	const FMapWardenRules& Rules,
	const FMapWardenScanContext& Context)
{
	using namespace MapWardenRules;

	const double Start = FPlatformTime::Seconds();

	FMapWardenReport Report;
	Report.bHasRun = true;
	Report.LevelName = Context.LevelName;
	Report.LoadedLevels = Context.LoadedLevels;
	Report.bWorldPartition = Context.bWorldPartition;
	Report.SourceName = Context.SourceName;
	Report.ActorsChecked = Actors.Num();
	Report.ActorsSkipped = Context.ActorsSkipped;
	Report.BoundsRadiusCm = Rules.OutOfBoundsRadiusCm;
	Report.Centre = ComputeCentre(Actors);

	//~ 1. Duplicates -------------------------------------------------------------------------------------
	if (Rules.bCheckDuplicates)
	{
		TArray<TPair<int32, int32>> Pairs;
		FindDuplicatePairs(Actors, Rules, Pairs);

		for (const TPair<int32, int32>& Pair : Pairs)
		{
			const FMapWardenActorInfo& First = Actors[Pair.Key];
			const FMapWardenActorInfo& Second = Actors[Pair.Value];

			// Reported against the second of the pair, because the first one is the one that was there
			// before somebody pressed Ctrl+D and it is almost always the one to keep.
			FMapWardenFinding Finding = Begin(Second, EMapFindingKind::Duplicate, Rules.DuplicateSeverity);
			Finding.OtherName = FName(*(First.Label.IsEmpty() ? First.Name.ToString() : First.Label));
			Finding.MeasuredValue = static_cast<float>(FVector::Dist(First.Location, Second.Location));
			Finding.Threshold = Rules.DuplicateToleranceCm;
			Finding.Unit = TEXT("cm");
			Report.Findings.Add(Finish(MoveTemp(Finding), Second, Rules));
		}

		Report.ChecksRun.Add(FString::Printf(
			TEXT("duplicates: same class, within %.2f cm, %.1f deg and %.0f%% scale of each other"),
			Rules.DuplicateToleranceCm, Rules.DuplicateRotationToleranceDeg, Rules.DuplicateScaleTolerance * 100.0f));
	}
	else
	{
		Report.ChecksRun.Add(TEXT("duplicates: NOT CHECKED (switched off in settings)"));
	}

	//~ 2. Collision --------------------------------------------------------------------------------------
	if (Rules.bCheckMissingCollision)
	{
		for (const FMapWardenActorInfo& Actor : Actors)
		{
			if (!Actor.bHasStaticMesh || !Actor.bCollisionDisabled || !Actor.bMeshHasCollisionModel)
			{
				continue;
			}

			FMapWardenFinding Finding = Begin(Actor, EMapFindingKind::MissingCollision, Rules.MissingCollisionSeverity);
			Finding.Context = Actor.MeshName.ToString();
			Report.Findings.Add(Finish(MoveTemp(Finding), Actor, Rules));
		}

		Report.ChecksRun.Add(TEXT("collision: static meshes set to No Collision whose mesh does have a collision model"));
	}
	else
	{
		Report.ChecksRun.Add(TEXT("collision: NOT CHECKED (switched off in settings)"));
	}

	//~ 3. Mobility ---------------------------------------------------------------------------------------
	if (Rules.bCheckMobility)
	{
		for (const FMapWardenActorInfo& Actor : Actors)
		{
			if (!Actor.bMovable || (!Actor.bHasLight && !Actor.bHasStaticMesh))
			{
				continue;
			}

			// Four separate ways of being allowed to be Movable, and all four have to be absent before this
			// is worth saying. Anything that moves - or is moved, or is attached to something that moves -
			// is doing what Movable is for.
			if (Actor.bHasMovementComponent || Actor.bSimulatesPhysics || Actor.bAttachedToMovable || Actor.bReferencedBySequence)
			{
				continue;
			}

			FMapWardenFinding Finding = Begin(Actor, EMapFindingKind::Mobility, Rules.MobilitySeverity);
			Finding.Context = Actor.bHasLight ? TEXT("light") : TEXT("mesh");
			Report.Findings.Add(Finish(MoveTemp(Finding), Actor, Rules));
		}

		Report.ChecksRun.Add(TEXT("mobility: Movable lights and meshes with no movement, no physics and no sequence"));
	}
	else
	{
		Report.ChecksRun.Add(TEXT("mobility: NOT CHECKED (switched off in settings)"));
	}

	//~ 4. Out of bounds ----------------------------------------------------------------------------------
	if (Rules.bCheckOutOfBounds)
	{
		for (const FMapWardenActorInfo& Actor : Actors)
		{
			if (!IsOutOfBounds(Actor.Location, Report.Centre, Rules.OutOfBoundsRadiusCm))
			{
				continue;
			}

			FMapWardenFinding Finding = Begin(Actor, EMapFindingKind::OutOfBounds, Rules.OutOfBoundsSeverity);
			Finding.MeasuredValue = static_cast<float>(FVector::Dist(Actor.Location, Report.Centre));
			Finding.Threshold = Rules.OutOfBoundsRadiusCm;
			Finding.Unit = TEXT("cm");
			Report.Findings.Add(Finish(MoveTemp(Finding), Actor, Rules));
		}

		Report.ChecksRun.Add(FString::Printf(
			TEXT("bounds: further than %.0f cm from the centre of everything checked"), Rules.OutOfBoundsRadiusCm));
	}
	else
	{
		Report.ChecksRun.Add(TEXT("bounds: NOT CHECKED (switched off in settings)"));
	}

	//~ 5. Silent triggers --------------------------------------------------------------------------------
	if (Rules.bCheckSilentTriggers)
	{
		// Clamped to Warning whatever the settings say. A binding can be made at runtime, by a Blueprint
		// that has not been loaded yet, and a check that cannot see that must not be allowed to fail
		// somebody's build over it. The sentence on the finding says the same thing in words.
		const EMapSeverity Severity = ClampTo(Rules.SilentTriggerSeverity, EMapSeverity::Warning);

		for (const FMapWardenActorInfo& Actor : Actors)
		{
			if (!Actor.bIsOverlapTrigger || Actor.bHasOverlapBinding || Actor.bReferencedByLevelBlueprint)
			{
				continue;
			}

			Report.Findings.Add(Finish(Begin(Actor, EMapFindingKind::SilentTrigger, Severity), Actor, Rules));
		}

		Report.ChecksRun.Add(TEXT("triggers: overlap volumes with no delegate bound and no level blueprint reference"));
	}
	else
	{
		Report.ChecksRun.Add(TEXT("triggers: NOT CHECKED (switched off in settings)"));
	}

	//~ 6. Dead references --------------------------------------------------------------------------------
	if (Rules.bCheckDeadReferences)
	{
		for (const FMapWardenActorInfo& Actor : Actors)
		{
			for (const FMapWardenDeadReference& Dead : Actor.DeadReferences)
			{
				FMapWardenFinding Finding = Begin(Actor, EMapFindingKind::DeadReference, Rules.DeadReferenceSeverity);
				Finding.OtherName = Dead.PropertyName;
				Finding.Context = Dead.TargetPath;
				Report.Findings.Add(Finish(MoveTemp(Finding), Actor, Rules));
			}
		}

		Report.ChecksRun.Add(TEXT("references: soft references on placed actors that no longer resolve"));
	}
	else
	{
		Report.ChecksRun.Add(TEXT("references: NOT CHECKED (switched off in settings)"));
	}

	//~ 7. Default names ----------------------------------------------------------------------------------
	if (Rules.bCheckDefaultNames)
	{
		int32 DefaultNamed = 0;
		TArray<FString> Examples;

		for (const FMapWardenActorInfo& Actor : Actors)
		{
			if (!Actor.bDefaultName)
			{
				continue;
			}

			++DefaultNamed;
			if (Examples.Num() < 3)
			{
				Examples.Add(Actor.Label.IsEmpty() ? Actor.Name.ToString() : Actor.Label);
			}
		}

		if (DefaultNamed > Rules.DefaultNameLimit)
		{
			// One finding about the level rather than one per actor. A thousand separate lines saying
			// "this is called StaticMeshActor_231" is not a report, it is a denial of service on the reader.
			FMapWardenFinding Finding;
			Finding.Kind = EMapFindingKind::DefaultName;
			Finding.Severity = Rules.DefaultNameSeverity;
			Finding.ActorName = FName(*Context.LevelName);
			Finding.MeasuredValue = static_cast<float>(DefaultNamed);
			Finding.Threshold = static_cast<float>(Rules.DefaultNameLimit);
			Finding.Unit = TEXT("actors");
			Finding.Context = FString::Join(Examples, TEXT(", "));
			Finding.Detail = Explain(Finding);
			Report.Findings.Add(MoveTemp(Finding));
		}

		Report.ChecksRun.Add(FString::Printf(
			TEXT("names: more than %d actors still called what their class called them (%d found)"),
			Rules.DefaultNameLimit, DefaultNamed));
	}
	else
	{
		Report.ChecksRun.Add(TEXT("names: NOT CHECKED (off by default - how many default names is taste)"));
	}

	//~ Counts, order and verdict -------------------------------------------------------------------------

	for (const FMapWardenFinding& Finding : Report.Findings)
	{
		switch (Finding.Severity)
		{
		case EMapSeverity::Error:	++Report.ErrorCount; break;
		case EMapSeverity::Warning:	++Report.WarningCount; break;
		default:					++Report.InfoCount; break;
		}

		Report.ExcludedCount += Finding.bExcluded ? 1 : 0;
	}

	// Errors first, then warnings, then info; within a severity by kind and then by actor name. Stable, so
	// two runs over an unchanged level produce a byte-identical report and a screenshot means one thing.
	Report.Findings.Sort([](const FMapWardenFinding& A, const FMapWardenFinding& B)
	{
		const int32 RankA = SeverityRank(A.Severity);
		const int32 RankB = SeverityRank(B.Severity);
		if (RankA != RankB)
		{
			return RankA < RankB;
		}

		if (A.Kind != B.Kind)
		{
			return static_cast<uint8>(A.Kind) < static_cast<uint8>(B.Kind);
		}

		return A.ActorName.LexicalLess(B.ActorName);
	});

	Report.Verdict = Judge(Report.Findings);
	Report.ScanMilliseconds = static_cast<float>((FPlatformTime::Seconds() - Start) * 1000.0);
	return Report;
}

//~ Names, numbers and formatting ---------------------------------------------------------------------------

FString UMapWardenStatics::VerdictName(const EMapVerdict Verdict)
{
	switch (Verdict)
	{
	case EMapVerdict::Fail:	return TEXT("fail");
	case EMapVerdict::Warn:	return TEXT("warn");
	default:				return TEXT("ok");
	}
}

int32 UMapWardenStatics::VerdictExitCode(const EMapVerdict Verdict)
{
	switch (Verdict)
	{
	case EMapVerdict::Fail:	return 2;
	case EMapVerdict::Warn:	return 1;
	default:				return 0;
	}
}

FString UMapWardenStatics::SeverityName(const EMapSeverity Severity)
{
	switch (Severity)
	{
	case EMapSeverity::Error:	return TEXT("error");
	case EMapSeverity::Warning:	return TEXT("warning");
	default:					return TEXT("info");
	}
}

FString UMapWardenStatics::KindName(const EMapFindingKind Kind)
{
	switch (Kind)
	{
	case EMapFindingKind::Duplicate:		return TEXT("duplicate");
	case EMapFindingKind::MissingCollision:	return TEXT("collision");
	case EMapFindingKind::Mobility:			return TEXT("mobility");
	case EMapFindingKind::OutOfBounds:		return TEXT("bounds");
	case EMapFindingKind::SilentTrigger:	return TEXT("trigger");
	case EMapFindingKind::DeadReference:	return TEXT("reference");
	default:								return TEXT("name");
	}
}

FString UMapWardenStatics::FormatCount(const int32 Count)
{
	// By hand rather than through FText::AsNumber. The report is a screenshot as often as it is a log line,
	// and a number that reads 4,812 on one machine and 4.812 on another is a number two people cannot talk
	// about.
	const bool bNegative = Count < 0;
	FString Digits = FString::FromInt(bNegative ? -Count : Count);

	for (int32 Index = Digits.Len() - 3; Index > 0; Index -= 3)
	{
		Digits.InsertAt(Index, TEXT(","));
	}

	return bNegative ? (TEXT("-") + Digits) : Digits;
}

FString UMapWardenStatics::FormatHeadline(const FMapWardenReport& Report)
{
	if (!Report.bHasRun)
	{
		return TEXT("MapWarden NOT RUN | nothing has been checked yet");
	}

	TStringBuilder<512> Line;
	Line.Appendf(TEXT("MapWarden %s | actors %s checked in %s | errors %d  warnings %d  info %d | %d excluded by settings | scan %.0f ms"),
		*VerdictName(Report.Verdict).ToUpper(),
		*FormatCount(Report.ActorsChecked),
		Report.LevelName.IsEmpty() ? TEXT("unknown level") : *Report.LevelName,
		Report.ErrorCount,
		Report.WarningCount,
		Report.InfoCount,
		Report.ExcludedCount,
		Report.ScanMilliseconds);

	return FString(Line.ToView());
}

FString UMapWardenStatics::FormatFinding(const FMapWardenFinding& Finding)
{
	TStringBuilder<512> Line;

	Line.Appendf(TEXT("%-7s %-9s %s"),
		*SeverityName(Finding.Severity),
		*KindName(Finding.Kind),
		*Finding.ActorName.ToString());

	switch (Finding.Kind)
	{
	case EMapFindingKind::Duplicate:
		Line.Appendf(TEXT(" sits %.2f cm from %s, same rotation and scale (tolerance %.2f cm)"),
			Finding.MeasuredValue, *Finding.OtherName.ToString(), Finding.Threshold);
		break;

	case EMapFindingKind::MissingCollision:
		Line.Appendf(TEXT(" is set to No Collision but %s has a collision model"), *Finding.Context);
		break;

	case EMapFindingKind::Mobility:
		Line.Appendf(TEXT(" is a Movable %s that nothing moves"), *Finding.Context);
		break;

	case EMapFindingKind::OutOfBounds:
		Line.Appendf(TEXT(" is %s cm from the centre of the level (limit %s cm)"),
			*FormatCount(FMath::RoundToInt(Finding.MeasuredValue)),
			*FormatCount(FMath::RoundToInt(Finding.Threshold)));
		break;

	case EMapFindingKind::SilentTrigger:
		Line.Append(TEXT(" overlaps, and nothing is listening (runtime bindings are not visible here)"));
		break;

	case EMapFindingKind::DeadReference:
		Line.Appendf(TEXT(".%s points at %s, which is not there"), *Finding.OtherName.ToString(), *Finding.Context);
		break;

	case EMapFindingKind::DefaultName:
		Line.Appendf(TEXT(" has %s actors still named after their class (limit %s), e.g. %s"),
			*FormatCount(FMath::RoundToInt(Finding.MeasuredValue)),
			*FormatCount(FMath::RoundToInt(Finding.Threshold)),
			*Finding.Context);
		break;
	}

	if (Finding.bExcluded)
	{
		Line.Append(TEXT("   [excluded by settings]"));
	}

	return FString(Line.ToView());
}

FString UMapWardenStatics::FormatCoverage(const FMapWardenReport& Report)
{
	TStringBuilder<512> Line;

	if (Report.bWorldPartition)
	{
		// The single most important line on a partitioned world. Only the cells in memory were checked, and
		// a report that did not say so would be certifying a level it never saw.
		Line.Appendf(TEXT("World Partition: %s actors in %d loaded cell(s) checked - UNLOADED CELLS WERE NOT CHECKED"),
			*FormatCount(Report.ActorsChecked), Report.LoadedLevels.Num());
	}
	else
	{
		Line.Appendf(TEXT("%s actors checked in %d level(s)"),
			*FormatCount(Report.ActorsChecked), FMath::Max(Report.LoadedLevels.Num(), 1));
	}

	if (Report.ActorsSkipped > 0)
	{
		Line.Appendf(TEXT("; %s not checked (spawned at runtime, editor-only or on the ignore list)"),
			*FormatCount(Report.ActorsSkipped));
	}

	if (!Report.SourceName.IsEmpty())
	{
		Line.Appendf(TEXT("; source %s"), *Report.SourceName);
	}

	return FString(Line.ToView());
}

FString UMapWardenStatics::Explain(const FMapWardenFinding& Finding)
{
	FString Sentence;

	switch (Finding.Kind)
	{
	case EMapFindingKind::Duplicate:
		Sentence = FString::Printf(
			TEXT("%s and %s are the same class in the same place with the same rotation and scale. One of them is invisible behind the other, and it still costs memory, a draw call and sometimes z-fighting. Delete one, or move it if the stack was deliberate."),
			*Finding.ActorName.ToString(), *Finding.OtherName.ToString());
		break;

	case EMapFindingKind::MissingCollision:
		Sentence = FString::Printf(
			TEXT("%s is placed with collision switched off, but its mesh (%s) does have a collision model, so somebody built that collision and this placement throws it away. Set Collision Presets back on the component, or exempt this actor if it is decoration nothing can reach."),
			*Finding.ActorName.ToString(), *Finding.Context);
		break;

	case EMapFindingKind::Mobility:
		Sentence = FString::Printf(
			TEXT("%s is Movable, and nothing in this level moves it: no movement component, no simulating body, no attachment to anything movable, no reference from a level sequence. Set it to Static, or add it to the exemptions if it is moved from code MapWarden cannot see."),
			*Finding.ActorName.ToString());
		break;

	case EMapFindingKind::OutOfBounds:
		Sentence = FString::Printf(
			TEXT("%s stands %s cm from the centre of everything else in this level. That is usually an actor somebody lost while dragging. Move it back, delete it, or raise the radius if your level really is that big."),
			*Finding.ActorName.ToString(), *FormatCount(FMath::RoundToInt(Finding.MeasuredValue)));
		break;

	case EMapFindingKind::SilentTrigger:
		// The honest sentence. This finding is a suspicion, it says so, and it can never be an error.
		Sentence = FString::Printf(
			TEXT("%s generates overlap events and nothing was found listening to them - no delegate bound when this scan ran, no reference from the level blueprint. Bindings made at runtime are not visible to this check, so this is a warning and never an error. If nothing does bind it, the volume is doing work for nobody."),
			*Finding.ActorName.ToString());
		break;

	case EMapFindingKind::DeadReference:
		Sentence = FString::Printf(
			TEXT("%s.%s points at %s, and that is not there any more. At runtime this will be a null: whatever it was supposed to open, play or spawn will not happen. Repoint it or clear it."),
			*Finding.ActorName.ToString(), *Finding.OtherName.ToString(), *Finding.Context);
		break;

	case EMapFindingKind::DefaultName:
		Sentence = FString::Printf(
			TEXT("%s actors in this level are still called what their class called them, which is over the limit of %s. Nothing is broken; it is a level nobody can navigate in the outliner. This check is off by default for exactly that reason."),
			*FormatCount(FMath::RoundToInt(Finding.MeasuredValue)),
			*FormatCount(FMath::RoundToInt(Finding.Threshold)));
		break;
	}

	if (Finding.bExcluded)
	{
		Sentence += TEXT(" This finding was excluded by the exemption lists in Project Settings - it is counted and shown, but it does not affect the verdict.");
	}

	return Sentence;
}

FString UMapWardenStatics::ReportToJson(const FMapWardenReport& Report)
{
	// Hand-shaped rather than reflected out of the struct. These field names are what a build script greps
	// for, which makes them a published interface - and a published interface must not change because
	// somebody renamed a C++ member.
	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();

	Root->SetStringField(TEXT("tool"), TEXT("MapWarden"));
	Root->SetStringField(TEXT("version"), TEXT("1.0.0"));
	Root->SetStringField(TEXT("verdict"), VerdictName(Report.Verdict));
	Root->SetNumberField(TEXT("exitCode"), VerdictExitCode(Report.Verdict));
	Root->SetStringField(TEXT("level"), Report.LevelName);
	Root->SetStringField(TEXT("source"), Report.SourceName);

	Root->SetNumberField(TEXT("actorsChecked"), Report.ActorsChecked);
	Root->SetNumberField(TEXT("actorsSkipped"), Report.ActorsSkipped);
	Root->SetNumberField(TEXT("errors"), Report.ErrorCount);
	Root->SetNumberField(TEXT("warnings"), Report.WarningCount);
	Root->SetNumberField(TEXT("info"), Report.InfoCount);
	Root->SetNumberField(TEXT("excludedBySettings"), Report.ExcludedCount);
	Root->SetNumberField(TEXT("scanMilliseconds"), Report.ScanMilliseconds);

	// Coverage is nested and flagged rather than flattened in beside the counts, so a script cannot read
	// "0 errors" without also being handed the fact that half the world was not in memory.
	const TSharedRef<FJsonObject> Coverage = MakeShared<FJsonObject>();
	Coverage->SetBoolField(TEXT("worldPartition"), Report.bWorldPartition);
	Coverage->SetBoolField(TEXT("complete"), !Report.bWorldPartition);
	Coverage->SetNumberField(TEXT("boundsRadiusCm"), Report.BoundsRadiusCm);
	Coverage->SetStringField(TEXT("summary"), FormatCoverage(Report));

	TArray<TSharedPtr<FJsonValue>> Levels;
	for (const FString& Level : Report.LoadedLevels)
	{
		Levels.Add(MakeShared<FJsonValueString>(Level));
	}
	Coverage->SetArrayField(TEXT("loadedLevels"), Levels);
	Root->SetObjectField(TEXT("coverage"), Coverage);

	TArray<TSharedPtr<FJsonValue>> Checks;
	for (const FString& Check : Report.ChecksRun)
	{
		Checks.Add(MakeShared<FJsonValueString>(Check));
	}
	Root->SetArrayField(TEXT("checks"), Checks);

	TArray<TSharedPtr<FJsonValue>> Findings;
	for (const FMapWardenFinding& Finding : Report.Findings)
	{
		const TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("kind"), KindName(Finding.Kind));
		Entry->SetStringField(TEXT("severity"), SeverityName(Finding.Severity));
		Entry->SetStringField(TEXT("actor"), Finding.ActorName.ToString());
		Entry->SetStringField(TEXT("actorPath"), Finding.ActorPath);
		Entry->SetStringField(TEXT("class"), Finding.ActorClass.ToString());
		Entry->SetStringField(TEXT("other"), Finding.OtherName.ToString());
		Entry->SetStringField(TEXT("context"), Finding.Context);
		Entry->SetNumberField(TEXT("measured"), Finding.MeasuredValue);
		Entry->SetNumberField(TEXT("threshold"), Finding.Threshold);
		Entry->SetStringField(TEXT("unit"), Finding.Unit);
		Entry->SetBoolField(TEXT("excludedBySettings"), Finding.bExcluded);
		Entry->SetStringField(TEXT("detail"), Finding.Detail);

		const TSharedRef<FJsonObject> Location = MakeShared<FJsonObject>();
		Location->SetNumberField(TEXT("x"), Finding.Location.X);
		Location->SetNumberField(TEXT("y"), Finding.Location.Y);
		Location->SetNumberField(TEXT("z"), Finding.Location.Z);
		Entry->SetObjectField(TEXT("location"), Location);

		Findings.Add(MakeShared<FJsonValueObject>(Entry));
	}
	Root->SetArrayField(TEXT("findings"), Findings);

	FString Output;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
	FJsonSerializer::Serialize(Root, Writer);
	return Output;
}

//~ Blueprint access ----------------------------------------------------------------------------------------

FMapWardenReport UMapWardenStatics::ScanNow(const UObject* WorldContextObject)
{
	if (UMapWardenSubsystem* Subsystem = UMapWardenSubsystem::Get(WorldContextObject))
	{
		return Subsystem->Scan();
	}

	return FMapWardenReport();
}

FMapWardenReport UMapWardenStatics::GetLastReport(const UObject* WorldContextObject)
{
	const UMapWardenSubsystem* Subsystem = UMapWardenSubsystem::Get(WorldContextObject);
	return Subsystem ? Subsystem->GetReport() : FMapWardenReport();
}

TArray<FMapWardenFinding> UMapWardenStatics::GetFindings(const UObject* WorldContextObject)
{
	const UMapWardenSubsystem* Subsystem = UMapWardenSubsystem::Get(WorldContextObject);
	return Subsystem ? Subsystem->GetReport().Findings : TArray<FMapWardenFinding>();
}

EMapVerdict UMapWardenStatics::GetVerdict(const UObject* WorldContextObject)
{
	const UMapWardenSubsystem* Subsystem = UMapWardenSubsystem::Get(WorldContextObject);
	return Subsystem ? Subsystem->GetVerdict() : EMapVerdict::Ok;
}

bool UMapWardenStatics::WriteReport(const UObject* WorldContextObject, const FString& Path)
{
	if (UMapWardenSubsystem* Subsystem = UMapWardenSubsystem::Get(WorldContextObject))
	{
		return Subsystem->WriteReport(Path);
	}

	return false;
}

bool UMapWardenStatics::FocusFinding(const UObject* WorldContextObject, const int32 FindingIndex)
{
	if (UMapWardenSubsystem* Subsystem = UMapWardenSubsystem::Get(WorldContextObject))
	{
		return Subsystem->FocusFinding(FindingIndex);
	}

	return false;
}

void UMapWardenStatics::SetExemptionsEnabled(const UObject* WorldContextObject, const bool bEnabled)
{
	if (UMapWardenSubsystem* Subsystem = UMapWardenSubsystem::Get(WorldContextObject))
	{
		Subsystem->SetExemptionsEnabled(bEnabled);
	}
}

bool UMapWardenStatics::AreExemptionsEnabled(const UObject* WorldContextObject)
{
	const UMapWardenSubsystem* Subsystem = UMapWardenSubsystem::Get(WorldContextObject);
	return Subsystem && Subsystem->AreExemptionsEnabled();
}

void UMapWardenStatics::SetReportVisible(const UObject* WorldContextObject, const bool bVisible)
{
	if (UMapWardenSubsystem* Subsystem = UMapWardenSubsystem::Get(WorldContextObject))
	{
		Subsystem->SetReportVisible(bVisible);
	}
}

bool UMapWardenStatics::IsReportVisible(const UObject* WorldContextObject)
{
	const UMapWardenSubsystem* Subsystem = UMapWardenSubsystem::Get(WorldContextObject);
	return Subsystem && Subsystem->IsReportVisible();
}
