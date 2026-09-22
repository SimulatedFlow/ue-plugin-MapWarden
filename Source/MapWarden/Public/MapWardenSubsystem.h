// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MapWardenTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "MapWardenSubsystem.generated.h"

class UCanvas;
class AHUD;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMapWardenFindingsSignature, const FMapWardenReport&, Report);

/**
 * The checker, per world.
 *
 * A world subsystem rather than a game instance subsystem, because the subject is the level, not the
 * session: two levels loaded one after the other are two different answers, and the report should not
 * outlive the thing it is about.
 *
 * It supports editor worlds as well as game and play-in-editor worlds. That is deliberate. Tools ->
 * MapWarden checks the level that is open right now, with nothing running, and a commandlet that loaded a
 * map for the gate has an editor world and no game at all.
 */
UCLASS()
class MAPWARDEN_API UMapWardenSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	//~ USubsystem interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	//~ UWorldSubsystem interface
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	/** The subsystem for whatever world this object is in, or null. */
	static UMapWardenSubsystem* Get(const UObject* WorldContextObject);

	//~ Checking ------------------------------------------------------------------------------------------

	/** Check the level now. Broadcasts OnFindings and returns the report. */
	UFUNCTION(BlueprintCallable, Category = "MapWarden")
	FMapWardenReport Scan();

	UFUNCTION(BlueprintPure, Category = "MapWarden")
	const FMapWardenReport& GetReport() const { return Report; }

	UFUNCTION(BlueprintPure, Category = "MapWarden")
	TArray<FMapWardenFinding> GetFindings() const { return Report.Findings; }

	UFUNCTION(BlueprintPure, Category = "MapWarden")
	EMapVerdict GetVerdict() const { return Report.Verdict; }

	/** Write the JSON report, checking first if nothing has been checked yet. */
	UFUNCTION(BlueprintCallable, Category = "MapWarden")
	bool WriteReport(const FString& Path);

	/**
	 * Select the actor behind finding number Index in the editor viewport.
	 *
	 * Returns false outside the editor, and that is the honest answer rather than a failure: in a packaged
	 * game there is no viewport and nothing to select.
	 */
	UFUNCTION(BlueprintCallable, Category = "MapWarden")
	bool FocusFinding(int32 FindingIndex);

	//~ The panel -----------------------------------------------------------------------------------------

	UFUNCTION(BlueprintCallable, Category = "MapWarden")
	void SetReportVisible(bool bVisible);

	UFUNCTION(BlueprintPure, Category = "MapWarden")
	bool IsReportVisible() const { return bReportVisible; }

	/** Use the exemption lists for this session, or do not. Re-checks immediately so you can see the difference. */
	UFUNCTION(BlueprintCallable, Category = "MapWarden")
	void SetExemptionsEnabled(bool bEnabled);

	UFUNCTION(BlueprintPure, Category = "MapWarden")
	bool AreExemptionsEnabled() const { return bExemptionsEnabled; }

	/** Draw the report on a canvas. Called by AMapWardenHUD, and by the any-HUD path. */
	void DrawReport(UCanvas* Canvas, const FVector2D& Origin, float Width) const;

	/** Fires after every check, so a UMG panel can rebuild itself without polling. */
	UPROPERTY(BlueprintAssignable, Category = "MapWarden")
	FMapWardenFindingsSignature OnFindings;

private:
	/** The project settings with the session's exemption override laid on top. */
	FMapWardenRules MakeRules() const;

	void HandleAutoScan();
	void HandleHUDPostRender(AHUD* HUD, UCanvas* Canvas);

	UPROPERTY()
	FMapWardenReport Report;

	bool bReportVisible = true;
	bool bExemptionsEnabled = true;

	FTimerHandle AutoScanTimer;
	FDelegateHandle HudPostRenderHandle;
};
