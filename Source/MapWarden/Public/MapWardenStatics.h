// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MapWardenTypes.h"
#include "MapWardenStatics.generated.h"

/**
 * The rules, and the Blueprint surface.
 *
 * Everything above the line is a static function over plain structs: no world, no subsystem, no actor. That
 * is why the rules are covered by automation tests, why the duplicate grid can be checked against a naive
 * comparison over five thousand actors, and why you can call any of this from your own tooling without
 * standing a game up. Everything below the line is the convenience layer a Blueprint calls, and it is a thin
 * wrapper over the subsystem.
 */
UCLASS()
class MAPWARDEN_API UMapWardenStatics : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	//~ The rules -----------------------------------------------------------------------------------------

	/**
	 * Are these two the same actor, twice?
	 *
	 * Same class, within ToleranceCm of each other, within RotationToleranceDeg on every axis and within
	 * ScaleTolerance on every axis. All four have to hold. Two crates a millimetre apart and turned ninety
	 * degrees are a wall somebody built on purpose; two crates a millimetre apart facing the same way are a
	 * Ctrl+D nobody moved.
	 *
	 * The boundary is inclusive: exactly ToleranceCm apart is within tolerance and is a duplicate. That is
	 * the same convention IsOutOfBounds uses - at exactly the threshold you are inside it - and it is tested.
	 */
	UFUNCTION(BlueprintPure, Category = "MapWarden|Rules")
	static bool AreDuplicates(
		const FMapWardenActorInfo& A,
		const FMapWardenActorInfo& B,
		float ToleranceCm,
		float RotationToleranceDeg,
		float ScaleTolerance);

	/**
	 * Is this position further from the centre than the radius allows?
	 *
	 * Exactly on the radius is inside it. An actor standing precisely on a round number of centimetres from
	 * the centre of the level is somebody's deliberate placement, not an accident, and a checker that
	 * reports the boundary case is a checker whose thresholds mean nothing.
	 */
	UFUNCTION(BlueprintPure, Category = "MapWarden|Rules")
	static bool IsOutOfBounds(const FVector& Position, const FVector& Centre, float RadiusCm);

	/**
	 * The verdict: Fail if there is at least one Error, Warn if there is at least one Warning, otherwise Ok.
	 *
	 * Nothing else. An Info finding, however many of them there are, never fails anything - which is what
	 * makes it safe to leave the default-name check on in a project that has thirty thousand of them.
	 */
	UFUNCTION(BlueprintPure, Category = "MapWarden|Rules")
	static EMapVerdict Judge(const TArray<FMapWardenFinding>& Findings);

	/** One plain sentence about what to do with this finding. */
	UFUNCTION(BlueprintPure, Category = "MapWarden|Rules")
	static FString Explain(const FMapWardenFinding& Finding);

	/** True when one of the three exemption lists covers this actor. */
	UFUNCTION(BlueprintPure, Category = "MapWarden|Rules")
	static bool IsExempt(const FMapWardenActorInfo& Actor, const FMapWardenRules& Rules);

	/** The average position of every actor handed in. The centre the bounds check measures from. */
	UFUNCTION(BlueprintPure, Category = "MapWarden|Rules")
	static FVector ComputeCentre(const TArray<FMapWardenActorInfo>& Actors);

	/**
	 * Every duplicate pair among these actors.
	 *
	 * This is the one part of MapWarden that would be naively quadratic, and on a level with twenty thousand
	 * actors that is two hundred million comparisons. It runs on a uniform grid whose cell size is the
	 * tolerance instead, so each actor is only ever compared against the handful in its own cell and the
	 * twenty-six around it, and the whole thing stays linear in the number of actors. Test six proves the
	 * grid finds exactly the pairs a naive comparison finds, over five thousand actors, because a fast
	 * search that quietly misses a pair is worse than a slow one.
	 *
	 * Each pair is reported once, with the lower index first.
	 */
	static void FindDuplicatePairs(
		const TArray<FMapWardenActorInfo>& Actors,
		const FMapWardenRules& Rules,
		TArray<TPair<int32, int32>>& OutPairs);

	/** The same, the slow way. Only used to check the grid in the tests - and to be honest about the cost. */
	static void FindDuplicatePairsNaive(
		const TArray<FMapWardenActorInfo>& Actors,
		const FMapWardenRules& Rules,
		TArray<TPair<int32, int32>>& OutPairs);

	/**
	 * Run every enabled check over these actors and produce the report.
	 *
	 * No world is involved. The scanner turns a world into an array of FMapWardenActorInfo and hands it
	 * here; a test builds the same array by hand.
	 */
	static FMapWardenReport Analyze(
		const TArray<FMapWardenActorInfo>& Actors,
		const FMapWardenRules& Rules,
		const FMapWardenScanContext& Context);

	//~ Names, numbers and formatting ---------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "MapWarden|Report")
	static FString VerdictName(EMapVerdict Verdict);

	/** 0 clean, 1 warnings, 2 errors. The same three numbers the other gate plugins return. */
	UFUNCTION(BlueprintPure, Category = "MapWarden|Report")
	static int32 VerdictExitCode(EMapVerdict Verdict);

	UFUNCTION(BlueprintPure, Category = "MapWarden|Report")
	static FString SeverityName(EMapSeverity Severity);

	UFUNCTION(BlueprintPure, Category = "MapWarden|Report")
	static FString KindName(EMapFindingKind Kind);

	/** 4812 -> "4,812". Done by hand rather than through FText so a screenshot reads the same in every locale. */
	UFUNCTION(BlueprintPure, Category = "MapWarden|Report")
	static FString FormatCount(int32 Count);

	/** The headline: actors checked, in which level, errors, warnings, info, excluded, milliseconds. */
	UFUNCTION(BlueprintPure, Category = "MapWarden|Report")
	static FString FormatHeadline(const FMapWardenReport& Report);

	/** One finding as one line: severity, kind, actor, what was measured against what. */
	UFUNCTION(BlueprintPure, Category = "MapWarden|Report")
	static FString FormatFinding(const FMapWardenFinding& Finding);

	/**
	 * The sentence about coverage.
	 *
	 * On a partitioned world it says, in words, that unloaded cells were not checked. Everywhere else it
	 * names the levels that were in memory. Clean must never be allowed to mean nobody looked, and this is
	 * the line that enforces it.
	 */
	UFUNCTION(BlueprintPure, Category = "MapWarden|Report")
	static FString FormatCoverage(const FMapWardenReport& Report);

	/** The report as JSON. The field names are a published interface; build scripts grep them. */
	UFUNCTION(BlueprintPure, Category = "MapWarden|Report")
	static FString ReportToJson(const FMapWardenReport& Report);

	//~ Blueprint access ----------------------------------------------------------------------------------

	/** Check the level now and return the report. */
	UFUNCTION(BlueprintCallable, Category = "MapWarden", meta = (WorldContext = "WorldContextObject"))
	static FMapWardenReport ScanNow(const UObject* WorldContextObject);

	/** The last report, without running a new check. */
	UFUNCTION(BlueprintPure, Category = "MapWarden", meta = (WorldContext = "WorldContextObject"))
	static FMapWardenReport GetLastReport(const UObject* WorldContextObject);

	UFUNCTION(BlueprintPure, Category = "MapWarden", meta = (WorldContext = "WorldContextObject"))
	static TArray<FMapWardenFinding> GetFindings(const UObject* WorldContextObject);

	UFUNCTION(BlueprintPure, Category = "MapWarden", meta = (WorldContext = "WorldContextObject"))
	static EMapVerdict GetVerdict(const UObject* WorldContextObject);

	/** Write the JSON report. An empty path means the one in Project Settings. */
	UFUNCTION(BlueprintCallable, Category = "MapWarden", meta = (WorldContext = "WorldContextObject"))
	static bool WriteReport(const UObject* WorldContextObject, const FString& Path);

	/**
	 * Select the actor a finding is about, in the editor viewport, and move the camera to it.
	 *
	 * Does nothing outside the editor, and says so by returning false. The runtime module has no business
	 * knowing what a viewport is; this asks through a delegate the editor module fills in.
	 */
	UFUNCTION(BlueprintCallable, Category = "MapWarden", meta = (WorldContext = "WorldContextObject"))
	static bool FocusFinding(const UObject* WorldContextObject, int32 FindingIndex);

	/** Use the exemption lists, or do not, for this session. Re-checks immediately. */
	UFUNCTION(BlueprintCallable, Category = "MapWarden", meta = (WorldContext = "WorldContextObject"))
	static void SetExemptionsEnabled(const UObject* WorldContextObject, bool bEnabled);

	UFUNCTION(BlueprintPure, Category = "MapWarden", meta = (WorldContext = "WorldContextObject"))
	static bool AreExemptionsEnabled(const UObject* WorldContextObject);

	/** Show or hide the on-screen report. */
	UFUNCTION(BlueprintCallable, Category = "MapWarden", meta = (WorldContext = "WorldContextObject"))
	static void SetReportVisible(const UObject* WorldContextObject, bool bVisible);

	UFUNCTION(BlueprintPure, Category = "MapWarden", meta = (WorldContext = "WorldContextObject"))
	static bool IsReportVisible(const UObject* WorldContextObject);
};
