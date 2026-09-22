// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "MapWardenHUD.h"

#include "Engine/Canvas.h"
#include "MapWardenSubsystem.h"

AMapWardenHUD::AMapWardenHUD()
{
	PrimaryActorTick.bCanEverTick = false;
}

void AMapWardenHUD::ToggleReport()
{
	if (UMapWardenSubsystem* Subsystem = UMapWardenSubsystem::Get(this))
	{
		Subsystem->SetReportVisible(!Subsystem->IsReportVisible());
	}
}

bool AMapWardenHUD::IsReportVisible() const
{
	const UMapWardenSubsystem* Subsystem = UMapWardenSubsystem::Get(this);
	return Subsystem && Subsystem->IsReportVisible();
}

void AMapWardenHUD::ScanNow()
{
	if (UMapWardenSubsystem* Subsystem = UMapWardenSubsystem::Get(this))
	{
		Subsystem->Scan();
	}
}

void AMapWardenHUD::DrawHUD()
{
	Super::DrawHUD();

	if (Canvas == nullptr)
	{
		return;
	}

	// Read from the subsystem on the frame it is drawn. Nothing is cached here, so the panel cannot claim
	// one verdict while the last check says another.
	const UMapWardenSubsystem* Subsystem = UMapWardenSubsystem::Get(this);
	if (Subsystem == nullptr || !Subsystem->IsReportVisible())
	{
		return;
	}

	Subsystem->DrawReport(Canvas, PanelOrigin, PanelWidth);
}
