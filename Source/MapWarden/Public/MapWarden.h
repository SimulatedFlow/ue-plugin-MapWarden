// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/**
 * The runtime module: the checks, the verdict, the report and the console surface.
 *
 * It has no startup work worth the name. Everything here is either a static rule, a world subsystem the
 * engine creates when a world appears, or a console command registered by its own static initialiser.
 */
class FMapWardenModule : public IModuleInterface
{
public:
	//~ IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
