// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "MapWardenFocus.h"

FMapWardenFocusDelegate& FMapWardenFocus::Get()
{
	// A function-local static rather than a file-scope one: this is asked for from module startup code, and
	// the order two translation units initialise their globals in is not something to bet a plugin on.
	static FMapWardenFocusDelegate Delegate;
	return Delegate;
}

bool FMapWardenFocus::Focus(const FString& ActorPath)
{
	if (ActorPath.IsEmpty() || !Get().IsBound())
	{
		return false;
	}

	return Get().Execute(ActorPath);
}
