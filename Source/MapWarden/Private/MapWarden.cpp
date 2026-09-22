// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "MapWarden.h"
#include "MapWardenLog.h"

DEFINE_LOG_CATEGORY(LogMapWarden);

#define LOCTEXT_NAMESPACE "FMapWardenModule"

void FMapWardenModule::StartupModule()
{
	UE_LOG(LogMapWarden, Log, TEXT("MapWarden started."));
}

void FMapWardenModule::ShutdownModule()
{
	UE_LOG(LogMapWarden, Log, TEXT("MapWarden shut down."));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FMapWardenModule, MapWarden)
