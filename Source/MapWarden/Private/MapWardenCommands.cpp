// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GenericPlatform/GenericPlatformMisc.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "MapWardenLog.h"
#include "MapWardenScanner.h"
#include "MapWardenSettings.h"
#include "MapWardenStatics.h"
#include "MapWardenSubsystem.h"

/**
 * The console surface.
 *
 * Every one of these works with a game running and without one. With a game, the check goes through the
 * world subsystem so the on-screen panel updates with it; without a game - in the editor, or from a
 * commandlet that has loaded a map - it runs the same rules over the editor world and writes the same file.
 *
 * That split is the reason these commands do not simply require a subsystem. A build server has no player,
 * and a build server is exactly where a level check pays for itself.
 */
namespace MapWardenCommands
{
	static bool ParseBool(const TArray<FString>& Args, const bool bDefault)
	{
		if (Args.Num() == 0)
		{
			return bDefault;
		}

		return Args[0].ToBool() || Args[0] == TEXT("1");
	}

	/** The subsystem of the best world there is, if that world has one. */
	static UMapWardenSubsystem* FindSubsystem()
	{
		UWorld* World = FMapWardenScanner::FindBestWorld();
		return World ? World->GetSubsystem<UMapWardenSubsystem>() : nullptr;
	}

	/** Check through the subsystem when there is one, straight through the scanner when there is not. */
	static FMapWardenReport RunScan()
	{
		if (UMapWardenSubsystem* Subsystem = FindSubsystem())
		{
			return Subsystem->Scan();
		}

		return FMapWardenScanner::RunWithProjectSettings(FMapWardenScanner::FindBestWorld());
	}

	static FAutoConsoleCommand GScan(
		TEXT("MapWarden.Scan"),
		TEXT("MapWarden.Scan - check the loaded level now and print the headline."),
		FConsoleCommandDelegate::CreateStatic([]()
		{
			FMapWardenScanner::LogReport(RunScan(), /*bAllFindings=*/false);
		}));

	static FAutoConsoleCommand GDump(
		TEXT("MapWarden.Dump"),
		TEXT("MapWarden.Dump - the whole report to the log: every finding, its sentence, and what was checked."),
		FConsoleCommandDelegate::CreateStatic([]()
		{
			FMapWardenScanner::LogReport(RunScan(), /*bAllFindings=*/true);
		}));

	static FAutoConsoleCommand GShow(
		TEXT("MapWarden.Show"),
		TEXT("MapWarden.Show [0|1] - show the on-screen report."),
		FConsoleCommandWithArgsDelegate::CreateStatic([](const TArray<FString>& Args)
		{
			if (UMapWardenSubsystem* Subsystem = FindSubsystem())
			{
				Subsystem->SetReportVisible(ParseBool(Args, true));
			}
			else
			{
				UE_LOG(LogMapWarden, Warning, TEXT("MapWarden.Show needs a running game. Use MapWarden.Dump instead."));
			}
		}));

	static FAutoConsoleCommand GHide(
		TEXT("MapWarden.Hide"),
		TEXT("MapWarden.Hide - hide the on-screen report."),
		FConsoleCommandDelegate::CreateStatic([]()
		{
			if (UMapWardenSubsystem* Subsystem = FindSubsystem())
			{
				Subsystem->SetReportVisible(false);
			}
		}));

	static FAutoConsoleCommand GExempt(
		TEXT("MapWarden.Exempt"),
		TEXT("MapWarden.Exempt [0|1] - use the exemption lists, or do not, for this session. Re-checks immediately."),
		FConsoleCommandWithArgsDelegate::CreateStatic([](const TArray<FString>& Args)
		{
			UMapWardenSubsystem* Subsystem = FindSubsystem();
			if (Subsystem == nullptr)
			{
				UE_LOG(LogMapWarden, Warning, TEXT("MapWarden.Exempt needs a world with a MapWarden subsystem."));
				return;
			}

			Subsystem->SetExemptionsEnabled(ParseBool(Args, true));
			UE_LOG(LogMapWarden, Display, TEXT("MapWarden: exemption lists %s."),
				Subsystem->AreExemptionsEnabled() ? TEXT("on") : TEXT("off"));
		}));

	static FAutoConsoleCommand GReport(
		TEXT("MapWarden.Report"),
		TEXT("MapWarden.Report [path] - check and write the JSON report. Default: the path in Project Settings."),
		FConsoleCommandWithArgsDelegate::CreateStatic([](const TArray<FString>& Args)
		{
			const FMapWardenReport Report = RunScan();

			FString FullPath;
			FMapWardenScanner::WriteReportFile(Report, Args.Num() > 0 ? Args[0] : FString(), FullPath);
		}));

	/**
	 * The gate.
	 *
	 * Checks the loaded level, writes Saved/MapWarden/report.json and ends the process with 0 when there is
	 * nothing to say, 1 when there are only warnings and 2 when there is an error. Those are the same three
	 * numbers AssetWarden, LocaleGuard, BindGuard, ReadGuard, WidgetLedger, LoadLens and HeapCensus return,
	 * and they mean the same three things - one convention, eight tools, nothing new to learn for a project
	 * that already owns one of them.
	 *
	 * -noexit reports without ending the process, which is what you want when you are typing this into the
	 * console rather than running it from a script.
	 */
	static void RunGate(const TArray<FString>& Args)
	{
		bool bExitWhenDone = true;
		FString Path;

		for (const FString& Arg : Args)
		{
			if (Arg.Equals(TEXT("-noexit"), ESearchCase::IgnoreCase))
			{
				bExitWhenDone = false;
			}
			else if (!Arg.StartsWith(TEXT("-")))
			{
				Path = Arg;
			}
		}

		const FMapWardenReport Report = RunScan();
		FMapWardenScanner::LogReport(Report, /*bAllFindings=*/true);

		FString FullPath;
		const bool bWritten = FMapWardenScanner::WriteReportFile(Report, Path, FullPath);

		const int32 ExitCode = UMapWardenStatics::VerdictExitCode(Report.Verdict);

		UE_LOG(LogMapWarden, Display, TEXT("MapWarden.Gate: %s - errors %d, warnings %d, %d excluded by settings. Exit code %d."),
			*UMapWardenStatics::VerdictName(Report.Verdict).ToUpper(),
			Report.ErrorCount, Report.WarningCount, Report.ExcludedCount, ExitCode);

		if (!bWritten)
		{
			UE_LOG(LogMapWarden, Error, TEXT("MapWarden.Gate: the report could not be written. Treating that as a failure."));
		}

		if (!Report.bHasRun)
		{
			UE_LOG(LogMapWarden, Error, TEXT("MapWarden.Gate: no level was loaded, so nothing was checked. That is not a pass."));
		}

		if (!bExitWhenDone)
		{
			return;
		}

		// A report that could not be written, or a gate that found no level at all, must never be allowed to
		// look like a gate that passed. Both leave with 2.
		const uint8 Status = (bWritten && Report.bHasRun) ? static_cast<uint8>(ExitCode) : 2;
		FPlatformMisc::RequestExitWithStatus(/*Force=*/true, Status, TEXT("MapWarden.Gate"));
	}

	static FAutoConsoleCommand GGate(
		TEXT("MapWarden.Gate"),
		TEXT("MapWarden.Gate [path] [-noexit] - check, write the report, exit 0 clean / 1 warnings / 2 errors."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&RunGate));
}
