// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MapWardenTypes.h"

class AActor;
class UWorld;

/**
 * The one place in this plugin that reads a world.
 *
 * It walks the actors, turns each one into an FMapWardenActorInfo - names, a transform and a handful of
 * booleans - and hands the array to UMapWardenStatics::Analyze. Everything after that point is pure. That
 * split is what makes the rules testable and it is what lets MapWarden.Gate run in a commandlet with a map
 * loaded and no game standing.
 */
struct MAPWARDEN_API FMapWardenScanner
{
	/** Turn one actor into the flat description the rules read. */
	static void DescribeActor(const AActor* Actor, const FMapWardenRules& Rules, FMapWardenActorInfo& Out);

	/** Every actor in the world that is the level designer's business, described. */
	static void GatherActors(
		const UWorld* World,
		const FMapWardenRules& Rules,
		TArray<FMapWardenActorInfo>& OutActors,
		FMapWardenScanContext& OutContext);

	/** Gather and analyse, with the rules handed in. */
	static FMapWardenReport Run(const UWorld* World, const FMapWardenRules& Rules);

	/** Gather and analyse with the rules from Project Settings. */
	static FMapWardenReport RunWithProjectSettings(const UWorld* World);

	/**
	 * The best world to check when nobody said which one.
	 *
	 * A running game first, then play-in-editor, then the editor world. A commandlet that loaded a map has
	 * an editor world and no game, and that is the case the gate is built for.
	 */
	static UWorld* FindBestWorld();

	/** Write the report to Path, or to the path in Project Settings when Path is empty. */
	static bool WriteReportFile(const FMapWardenReport& Report, const FString& Path, FString& OutFullPath);

	/** The report to the log: the headline always, every finding when asked. */
	static void LogReport(const FMapWardenReport& Report, bool bAllFindings);
};
