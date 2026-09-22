// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Delegates/DelegateCombinations.h"

/**
 * "Show me that actor."
 *
 * The runtime module produces a finding with an actor path in it and has no way to act on that path, because
 * selecting something in a viewport is an editor idea and the runtime module is not allowed to know the
 * editor exists. So it asks, through this delegate, and the editor module answers by binding to it at
 * startup.
 *
 * In a cooked build nothing is bound, Focus returns false, and the Blueprint node that called it does
 * nothing - which is the correct behaviour, not a degraded one. There is no viewport to jump to.
 */
DECLARE_DELEGATE_RetVal_OneParam(bool, FMapWardenFocusDelegate, const FString& /*ActorPath*/);

struct MAPWARDEN_API FMapWardenFocus
{
	/** The delegate itself. The editor module binds this in StartupModule and unbinds in ShutdownModule. */
	static FMapWardenFocusDelegate& Get();

	/** Ask whoever is listening to show this actor. False when nobody is - a cooked build, or a commandlet. */
	static bool Focus(const FString& ActorPath);
};
