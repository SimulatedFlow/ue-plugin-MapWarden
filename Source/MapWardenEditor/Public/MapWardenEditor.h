// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/**
 * The editor module: the entry under Tools, and the jump to the actor.
 *
 * Nothing here decides anything. The checks, the thresholds, the verdict and the report all live in the
 * runtime module so that they work in a commandlet and in a cooked build; this module only makes them
 * reachable from a menu and answers the runtime module's one question - "show me that actor" - which it
 * cannot answer for itself.
 */
class FMapWardenEditorModule : public IModuleInterface
{
public:
	//~ IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void RegisterMenus();

	void RunScanFromMenu();
	void RunReportFromMenu();
	void SelectFindingsFromMenu();

	/** Bound to FMapWardenFocus: find the actor by path, select it, and move the camera to it. */
	static bool FocusActor(const FString& ActorPath);
};
