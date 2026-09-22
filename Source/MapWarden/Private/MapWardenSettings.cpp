// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "MapWardenSettings.h"

UMapWardenSettings::UMapWardenSettings()
{
	// Engine bookkeeping that lives in every level and belongs to nobody. Ignored rather than exempted: an
	// exemption is a finding that was made and forgiven, and forgiving the world settings for existing would
	// put a line on every report that nobody can act on.
	//
	// Deliberately short. ABrush is not on it, because AVolume derives from ABrush and a list that ignored
	// brushes would quietly switch the silent-trigger check off for every trigger volume in the project. The
	// builder brush is handled by identity instead, in the scanner.
	IgnoredClasses = {
		TEXT("WorldSettings"),
		TEXT("LevelScriptActor"),
		TEXT("AbstractNavData"),
	};
}

FName UMapWardenSettings::GetCategoryName() const
{
	return TEXT("Plugins");
}

FName UMapWardenSettings::GetSectionName() const
{
	return TEXT("MapWarden");
}

const UMapWardenSettings& UMapWardenSettings::Get()
{
	const UMapWardenSettings* Settings = GetDefault<UMapWardenSettings>();
	check(Settings != nullptr);
	return *Settings;
}

FMapWardenRules UMapWardenSettings::MakeRules() const
{
	FMapWardenRules Rules;

	Rules.bCheckDuplicates = bCheckDuplicates;
	Rules.bCheckMissingCollision = bCheckMissingCollision;
	Rules.bCheckMobility = bCheckMobility;
	Rules.bCheckOutOfBounds = bCheckOutOfBounds;
	Rules.bCheckSilentTriggers = bCheckSilentTriggers;
	Rules.bCheckDeadReferences = bCheckDeadReferences;
	Rules.bCheckDefaultNames = bCheckDefaultNames;

	Rules.DuplicateToleranceCm = DuplicateToleranceCm;
	Rules.DuplicateRotationToleranceDeg = DuplicateRotationToleranceDeg;
	Rules.DuplicateScaleTolerance = DuplicateScaleTolerance;
	Rules.OutOfBoundsRadiusCm = OutOfBoundsRadiusCm;
	Rules.DefaultNameLimit = DefaultNameLimit;

	Rules.DuplicateSeverity = DuplicateSeverity;
	Rules.MissingCollisionSeverity = MissingCollisionSeverity;
	Rules.MobilitySeverity = MobilitySeverity;
	Rules.OutOfBoundsSeverity = OutOfBoundsSeverity;
	Rules.SilentTriggerSeverity = SilentTriggerSeverity;
	Rules.DeadReferenceSeverity = DeadReferenceSeverity;
	Rules.DefaultNameSeverity = DefaultNameSeverity;

	Rules.bUseExemptions = bUseExemptions;
	Rules.ExemptClasses = ExemptClasses;
	Rules.ExemptFolders = ExemptFolders;
	Rules.ExemptTags = ExemptTags;

	Rules.bOnlyPlacedActors = bOnlyPlacedActors;
	Rules.IgnoredClasses = IgnoredClasses;
	Rules.bResolveSoftReferences = bResolveSoftReferences;

	return Rules;
}
