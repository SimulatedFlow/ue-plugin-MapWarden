// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "MapWardenHUD.generated.h"

/**
 * Draws the report.
 *
 * On UCanvas and not in UMG, and that is the one design decision in this class. A level full of duplicated
 * actors and Movable lights costs you something in the cooked build, which is exactly the build where a UMG
 * debug panel has usually been stripped or never gets created. A panel drawn from AHUD::DrawHUD is there
 * with nothing else running.
 *
 * Set this as the HUD class on your game mode, or leave your own HUD alone and turn on "Auto Draw On Any
 * HUD" in Project Settings, which routes the identical panel through AHUD::OnHUDPostRender. The two paths
 * know about each other and cannot draw twice.
 */
UCLASS()
class MAPWARDEN_API AMapWardenHUD : public AHUD
{
	GENERATED_BODY()

public:
	AMapWardenHUD();

	//~ AHUD interface
	virtual void DrawHUD() override;

	/** Show or hide the panel. */
	UFUNCTION(BlueprintCallable, Category = "MapWarden")
	void ToggleReport();

	UFUNCTION(BlueprintPure, Category = "MapWarden")
	bool IsReportVisible() const;

	/** Check the level now and redraw with the result. */
	UFUNCTION(BlueprintCallable, Category = "MapWarden")
	void ScanNow();

	/** Top-left corner of the panel, in pixels. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MapWarden")
	FVector2D PanelOrigin = FVector2D(28.0f, 90.0f);

	/** Panel width in pixels. Wide by default: a finding names two actors and a measured value. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MapWarden")
	float PanelWidth = 980.0f;
};
