// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "MapWardenSubsystem.h"

#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/HUD.h"
#include "GlobalRenderResources.h"
#include "MapWardenFocus.h"
#include "MapWardenHUD.h"
#include "MapWardenLog.h"
#include "MapWardenScanner.h"
#include "MapWardenSettings.h"
#include "MapWardenStatics.h"
#include "Misc/StringBuilder.h"
#include "TimerManager.h"

namespace MapWardenPanel
{
	static constexpr float LineHeight = 15.0f;
	static constexpr float BoxPadding = 8.0f;

	static const FLinearColor PanelBackground(0.0f, 0.0f, 0.0f, 0.68f);
	static const FLinearColor HeadingColor(0.42f, 0.78f, 1.0f, 1.0f);
	static const FLinearColor BodyColor(0.90f, 0.90f, 0.90f, 1.0f);
	static const FLinearColor GoodColor(0.42f, 0.95f, 0.48f, 1.0f);
	static const FLinearColor WarnColor(0.98f, 0.78f, 0.35f, 1.0f);
	static const FLinearColor ErrorColor(1.0f, 0.40f, 0.36f, 1.0f);
	static const FLinearColor DimColor(0.62f, 0.62f, 0.62f, 1.0f);

	static const FLinearColor& SeverityColor(const EMapSeverity Severity)
	{
		switch (Severity)
		{
		case EMapSeverity::Error:	return ErrorColor;
		case EMapSeverity::Warning:	return WarnColor;
		default:					return DimColor;
		}
	}

	static void DrawFilledRect(UCanvas* Canvas, const FVector2D& Position, const FVector2D& Size, const FLinearColor& Color)
	{
		FCanvasTileItem Tile(Position, GWhiteTexture, Size, Color);
		Tile.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Tile);
	}
}

//~ Lifetime -----------------------------------------------------------------------------------------------

void UMapWardenSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	const UMapWardenSettings& Settings = UMapWardenSettings::Get();
	bReportVisible = Settings.bShowReportByDefault;
	bExemptionsEnabled = Settings.bUseExemptions;

	if (Settings.bAutoDrawOnAnyHUD)
	{
		HudPostRenderHandle = AHUD::OnHUDPostRender.AddUObject(this, &UMapWardenSubsystem::HandleHUDPostRender);
	}
}

void UMapWardenSubsystem::Deinitialize()
{
	if (HudPostRenderHandle.IsValid())
	{
		AHUD::OnHUDPostRender.Remove(HudPostRenderHandle);
		HudPostRenderHandle.Reset();
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(AutoScanTimer);
	}

	Super::Deinitialize();
}

bool UMapWardenSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	// Editor as well as game and play-in-editor, which the base class does not allow by default.
	//
	// That is the whole reason Tools -> MapWarden can check the level that is open right now with nothing
	// running, and it is also what a commandlet gets when it loads a map for MapWarden.Gate. A level checker
	// that only worked while somebody was playing would be a level checker nobody could put in a build step.
	return WorldType == EWorldType::Game
		|| WorldType == EWorldType::PIE
		|| WorldType == EWorldType::Editor;
}

void UMapWardenSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	const UMapWardenSettings& Settings = UMapWardenSettings::Get();
	if (!Settings.bScanOnBeginPlay)
	{
		return;
	}

	// Delayed, and the delay is the whole reason this works. Actors bind their overlap delegates in
	// BeginPlay, so a check that ran in the same frame would report every trigger in the level as silent -
	// a tool that is loudly wrong on its first frame.
	const float Delay = FMath::Max(Settings.AutoScanDelaySeconds, 0.01f);
	InWorld.GetTimerManager().SetTimer(
		AutoScanTimer, FTimerDelegate::CreateUObject(this, &UMapWardenSubsystem::HandleAutoScan), Delay, false);
}

UMapWardenSubsystem* UMapWardenSubsystem::Get(const UObject* WorldContextObject)
{
	UWorld* World = GEngine
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		: nullptr;

	return World ? World->GetSubsystem<UMapWardenSubsystem>() : nullptr;
}

//~ Checking -----------------------------------------------------------------------------------------------

FMapWardenRules UMapWardenSubsystem::MakeRules() const
{
	FMapWardenRules Rules = UMapWardenSettings::Get().MakeRules();

	// The session override sits on top of the settings, so MapWarden.Exempt 0 - and the demo's toggle - can
	// show the same level both ways without editing a config file.
	Rules.bUseExemptions = bExemptionsEnabled;
	return Rules;
}

FMapWardenReport UMapWardenSubsystem::Scan()
{
	Report = FMapWardenScanner::Run(GetWorld(), MakeRules());

	FMapWardenScanner::LogReport(Report, /*bAllFindings=*/false);

	OnFindings.Broadcast(Report);
	return Report;
}

void UMapWardenSubsystem::HandleAutoScan()
{
	Scan();
}

bool UMapWardenSubsystem::WriteReport(const FString& Path)
{
	if (!Report.bHasRun)
	{
		Scan();
	}

	FString FullPath;
	return FMapWardenScanner::WriteReportFile(Report, Path, FullPath);
}

bool UMapWardenSubsystem::FocusFinding(const int32 FindingIndex)
{
	if (!Report.Findings.IsValidIndex(FindingIndex))
	{
		return false;
	}

	// Asked for, not done. Selecting something in a viewport is an editor idea; this module is not allowed
	// to know the editor exists, so it hands over an actor path and the editor module - if there is one -
	// answers. In a packaged game nothing is bound and this returns false, which is the truth rather than a
	// failure: there is no viewport to jump to.
	return FMapWardenFocus::Focus(Report.Findings[FindingIndex].ActorPath);
}

//~ The panel ----------------------------------------------------------------------------------------------

void UMapWardenSubsystem::SetReportVisible(const bool bVisible)
{
	bReportVisible = bVisible;
}

void UMapWardenSubsystem::SetExemptionsEnabled(const bool bEnabled)
{
	if (bExemptionsEnabled == bEnabled)
	{
		return;
	}

	bExemptionsEnabled = bEnabled;

	// Re-checked immediately, because the point of the switch is to see the difference. A toggle that only
	// took effect on the next check would look like it had done nothing.
	if (Report.bHasRun)
	{
		Scan();
	}
}

void UMapWardenSubsystem::HandleHUDPostRender(AHUD* HUD, UCanvas* Canvas)
{
	if (!IsValid(HUD) || Canvas == nullptr || !bReportVisible)
	{
		return;
	}

	// The two drawing paths must not stack. An AMapWardenHUD draws the panel itself, so this one stands down
	// for it - otherwise a project that both reparented its HUD and turned on the setting would get the same
	// panel twice, half a pixel apart.
	if (HUD->IsA<AMapWardenHUD>())
	{
		return;
	}

	DrawReport(Canvas, FVector2D(28.0f, 90.0f), 980.0f);
}

void UMapWardenSubsystem::DrawReport(UCanvas* Canvas, const FVector2D& Origin, const float Width) const
{
	using namespace MapWardenPanel;

	if (Canvas == nullptr)
	{
		return;
	}

	UFont* Font = GEngine ? GEngine->GetSmallFont() : nullptr;
	if (Font == nullptr)
	{
		return;
	}

	const UMapWardenSettings& Settings = UMapWardenSettings::Get();
	const int32 MaxRows = FMath::Max(Settings.MaxReportRows, 1);

	// Worked out before anything is drawn, so the background is exactly as tall as the text and a report
	// with two findings does not sit in a panel sized for twenty.
	const int32 ShownFindings = FMath::Min(Report.Findings.Num(), MaxRows);
	const bool bTruncated = Report.Findings.Num() > ShownFindings;
	const bool bClean = Report.bHasRun && Report.ErrorCount == 0 && Report.WarningCount == 0;

	int32 LineCount = 2;										// headline, coverage
	LineCount += ShownFindings;
	LineCount += bTruncated ? 1 : 0;
	LineCount += bClean ? (1 + Report.ChecksRun.Num()) : 0;		// "nothing to report", then what was checked
	LineCount += Report.bHasRun ? 0 : 1;						// "no check yet"

	const float BoxHeight = LineCount * LineHeight + BoxPadding * 2.0f;
	DrawFilledRect(Canvas,
		FVector2D(Origin.X - BoxPadding, Origin.Y - BoxPadding),
		FVector2D(Width, BoxHeight),
		PanelBackground);

	float LineY = static_cast<float>(Origin.Y);
	auto DrawLine = [&](FStringView Line, const FLinearColor& Color)
	{
		FCanvasTextStringViewItem Item(FVector2D(Origin.X, LineY), Line, Font, Color);
		Canvas->DrawItem(Item);
		LineY += LineHeight;
	};

	// The headline carries the verdict and the colour, so the answer is readable from across the room and
	// before a single finding has been read.
	const FLinearColor& VerdictColor =
		(Report.Verdict == EMapVerdict::Fail) ? ErrorColor :
		(Report.Verdict == EMapVerdict::Warn) ? WarnColor : GoodColor;

	DrawLine(*UMapWardenStatics::FormatHeadline(Report), Report.bHasRun ? VerdictColor : HeadingColor);

	// The honesty line: how much of the level this report is actually about. On a partitioned world it says
	// that unloaded cells were not checked, and it says it in the same size text as everything else.
	DrawLine(*UMapWardenStatics::FormatCoverage(Report), Report.bWorldPartition ? WarnColor : DimColor);

	if (!Report.bHasRun)
	{
		DrawLine(TEXT("nothing checked yet - run MapWarden.Scan, or press the check button"), DimColor);
		return;
	}

	for (int32 Index = 0; Index < ShownFindings; ++Index)
	{
		const FMapWardenFinding& Finding = Report.Findings[Index];
		DrawLine(*UMapWardenStatics::FormatFinding(Finding), SeverityColor(Finding.Severity));
	}

	if (bTruncated)
	{
		TStringBuilder<128> Line;
		Line.Appendf(TEXT("... and %d more - MapWarden.Dump writes all of them to the log"),
			Report.Findings.Num() - ShownFindings);
		DrawLine(Line.ToView(), DimColor);
	}

	// Green is only allowed to mean something if it also says what it looked at. A checker that goes quiet
	// when it is happy is indistinguishable from a checker that never ran, and that is how a broken gate
	// stays broken for a milestone.
	if (bClean)
	{
		DrawLine(TEXT("nothing to report. checked:"), GoodColor);

		for (const FString& Check : Report.ChecksRun)
		{
			TStringBuilder<256> Line;
			Line.Appendf(TEXT("   %s"), *Check);
			DrawLine(Line.ToView(), Check.Contains(TEXT("NOT CHECKED")) ? WarnColor : DimColor);
		}
	}
}
