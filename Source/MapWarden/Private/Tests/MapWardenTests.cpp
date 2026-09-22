// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "MapWardenStatics.h"
#include "MapWardenTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace MapWardenTests
{
	// CommandletContext as well as EditorContext. Every rule in this plugin is a place it can be quietly
	// wrong, and a test that only runs when somebody has the editor open is a test that will not be there on
	// the build machine - which is exactly where this plugin is meant to live.
	constexpr EAutomationTestFlags TestFlags = EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::EngineFilter;

	/** One actor, the way the rules see it. */
	FMapWardenActorInfo Actor(
		const TCHAR* Name,
		const TCHAR* Class,
		const FVector& Location,
		const FRotator& Rotation = FRotator::ZeroRotator,
		const FVector& Scale = FVector::OneVector)
	{
		FMapWardenActorInfo Info;
		Info.Name = FName(Name);
		Info.Label = Name;
		Info.Path = FString(TEXT("/Game/Maps/L_Test.L_Test:PersistentLevel.")) + Name;
		Info.ClassName = FName(Class);
		Info.ClassChain = { FName(Class), TEXT("Actor") };
		Info.Location = Location;
		Info.Rotation = Rotation;
		Info.Scale = Scale;
		return Info;
	}

	FMapWardenScanContext Context()
	{
		FMapWardenScanContext Ctx;
		Ctx.LevelName = TEXT("L_Test");
		Ctx.LoadedLevels = { TEXT("L_Test") };
		Ctx.SourceName = TEXT("test");
		return Ctx;
	}

	/** Every check off. Tests switch on the one they are about, so nothing else can produce a finding. */
	FMapWardenRules NoChecks()
	{
		FMapWardenRules Rules;
		Rules.bCheckDuplicates = false;
		Rules.bCheckMissingCollision = false;
		Rules.bCheckMobility = false;
		Rules.bCheckOutOfBounds = false;
		Rules.bCheckSilentTriggers = false;
		Rules.bCheckDeadReferences = false;
		Rules.bCheckDefaultNames = false;
		return Rules;
	}

	int32 CountOfKind(const TArray<FMapWardenFinding>& Findings, const EMapFindingKind Kind)
	{
		int32 Count = 0;
		for (const FMapWardenFinding& Finding : Findings)
		{
			Count += (Finding.Kind == Kind) ? 1 : 0;
		}
		return Count;
	}

	/** A deterministic generator. Nothing in a test may depend on the machine it runs on. */
	struct FRandom
	{
		uint32 State = 0x9E3779B9u;

		double Next(const double Range)
		{
			State = State * 1664525u + 1013904223u;
			return (static_cast<double>(State >> 8) / static_cast<double>(1u << 24)) * Range;
		}
	};
}

//
// (1) AreDuplicates hits the tolerance exactly, and tells the same place apart from the same transform.
//
// This is the rule the headline finding stands on. If it were loose, every tiled floor in every project
// would be reported and the plugin would be uninstalled by lunchtime; if it were strict about rotation in
// the wrong direction, the actual Ctrl+D accident would slip through.
//
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMapWardenDuplicateToleranceTest,
	"MapWarden.Rules.AreDuplicatesRespectsToleranceRotationAndScale",
	MapWardenTests::TestFlags)

bool FMapWardenDuplicateToleranceTest::RunTest(const FString& Parameters)
{
	using namespace MapWardenTests;

	const FMapWardenActorInfo A = Actor(TEXT("SM_Crate_1"), TEXT("StaticMeshActor"), FVector(100.0, 200.0, 0.0));

	// Exactly on the tolerance is within the tolerance. Documented, and tested, because "within 1 cm" has to
	// mean one thing for the report to be arguable with.
	const FMapWardenActorInfo Exactly = Actor(TEXT("SM_Crate_2"), TEXT("StaticMeshActor"), FVector(101.0, 200.0, 0.0));
	TestTrue(TEXT("exactly one centimetre apart is a duplicate at a tolerance of one centimetre"),
		UMapWardenStatics::AreDuplicates(A, Exactly, 1.0f, 1.0f, 0.01f));

	const FMapWardenActorInfo JustOver = Actor(TEXT("SM_Crate_3"), TEXT("StaticMeshActor"), FVector(101.5, 200.0, 0.0));
	TestFalse(TEXT("one and a half centimetres apart is not"),
		UMapWardenStatics::AreDuplicates(A, JustOver, 1.0f, 1.0f, 0.01f));

	// The same place, turned. A wall built out of one mesh rotated four ways is not six mistakes.
	const FMapWardenActorInfo Rotated = Actor(TEXT("SM_Crate_4"), TEXT("StaticMeshActor"),
		FVector(100.0, 200.0, 0.0), FRotator(0.0, 90.0, 0.0));
	TestFalse(TEXT("same position, different rotation, is not a duplicate"),
		UMapWardenStatics::AreDuplicates(A, Rotated, 1.0f, 1.0f, 0.01f));

	// ...but 359 degrees and -1 degree are one degree apart, not 360. A naive subtraction gets this wrong and
	// would report a real duplicate as clean.
	const FMapWardenActorInfo NearlyAligned = Actor(TEXT("SM_Crate_5"), TEXT("StaticMeshActor"),
		FVector(100.0, 200.0, 0.0), FRotator(0.0, 359.5, 0.0));
	TestTrue(TEXT("359.5 degrees is half a degree from zero, the short way round"),
		UMapWardenStatics::AreDuplicates(A, NearlyAligned, 1.0f, 1.0f, 0.01f));

	// The same place, the same rotation, twice the size.
	const FMapWardenActorInfo Scaled = Actor(TEXT("SM_Crate_6"), TEXT("StaticMeshActor"),
		FVector(100.0, 200.0, 0.0), FRotator::ZeroRotator, FVector(2.0));
	TestFalse(TEXT("same position, different scale, is not a duplicate"),
		UMapWardenStatics::AreDuplicates(A, Scaled, 1.0f, 1.0f, 0.01f));

	// A different class in the same place is a decision - a pillar inside an alcove, a decal on a wall.
	const FMapWardenActorInfo OtherClass = Actor(TEXT("BP_Lamp_1"), TEXT("BP_Lamp"), FVector(100.0, 200.0, 0.0));
	TestFalse(TEXT("two different classes in one place are not a duplicate"),
		UMapWardenStatics::AreDuplicates(A, OtherClass, 1.0f, 1.0f, 0.01f));

	return true;
}

//
// (2) IsOutOfBounds at the boundary.
//
// Exactly on the radius is inside it. An actor standing precisely on a round number of centimetres from the
// centre is somebody's deliberate placement, and a checker that reports its own boundary is a checker whose
// thresholds mean nothing.
//
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMapWardenBoundsTest,
	"MapWarden.Rules.IsOutOfBoundsTreatsTheBoundaryAsInside",
	MapWardenTests::TestFlags)

bool FMapWardenBoundsTest::RunTest(const FString& Parameters)
{
	const FVector Centre(1000.0, -2000.0, 50.0);

	TestFalse(TEXT("the centre itself is inside"),
		UMapWardenStatics::IsOutOfBounds(Centre, Centre, 100000.0f));

	TestFalse(TEXT("exactly on the radius is inside"),
		UMapWardenStatics::IsOutOfBounds(Centre + FVector(100000.0, 0.0, 0.0), Centre, 100000.0f));

	TestTrue(TEXT("one centimetre past the radius is outside"),
		UMapWardenStatics::IsOutOfBounds(Centre + FVector(100001.0, 0.0, 0.0), Centre, 100000.0f));

	// Distance, not per-axis. An actor a hundred thousand centimetres out on each of three axes is a hundred
	// and seventy thousand away, and reporting it as inside would be a checker measuring the wrong thing.
	TestTrue(TEXT("the check is a distance, not a box"),
		UMapWardenStatics::IsOutOfBounds(Centre + FVector(70000.0, 70000.0, 70000.0), Centre, 100000.0f));

	// And the centre is the average of what was checked, not the origin: a level built two kilometres out is
	// an ordinary level, not a level in which everything is lost.
	const TArray<FMapWardenActorInfo> Actors = {
		MapWardenTests::Actor(TEXT("A"), TEXT("StaticMeshActor"), FVector(200000.0, 0.0, 0.0)),
		MapWardenTests::Actor(TEXT("B"), TEXT("StaticMeshActor"), FVector(200200.0, 0.0, 0.0)),
	};

	const FVector Computed = UMapWardenStatics::ComputeCentre(Actors);
	TestTrue(TEXT("the centre is the average position of everything checked"),
		Computed.Equals(FVector(200100.0, 0.0, 0.0), 0.01));

	return true;
}

//
// (3) An exempt actor produces no error, and is still counted.
//
// The exemption list is the reason this plugin survives its first run on a real level. It is also the most
// dangerous feature in it, because a list that could silence a finding without leaving a trace is a way to
// make a level green from a settings page. So: still made, still shown, forced to Info, counted as excluded.
//
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMapWardenExemptionTest,
	"MapWarden.Rules.ExemptActorProducesNoErrorButIsStillCounted",
	MapWardenTests::TestFlags)

bool FMapWardenExemptionTest::RunTest(const FString& Parameters)
{
	using namespace MapWardenTests;

	FMapWardenActorInfo Broken = Actor(TEXT("BP_Door_1"), TEXT("BP_Door"), FVector::ZeroVector);
	Broken.Tags.Add(TEXT("MapWarden.Ignore"));

	FMapWardenDeadReference Dead;
	Dead.PropertyName = TEXT("LinkedSwitch");
	Dead.TargetPath = TEXT("/Game/Old/BP_Switch.BP_Switch");
	Broken.DeadReferences.Add(Dead);

	FMapWardenRules Rules = NoChecks();
	Rules.bCheckDeadReferences = true;
	Rules.DeadReferenceSeverity = EMapSeverity::Error;

	// Without the exemption: an error, and the gate fails.
	{
		Rules.bUseExemptions = false;
		const FMapWardenReport Report = UMapWardenStatics::Analyze({ Broken }, Rules, Context());

		TestEqual(TEXT("the dead reference is found"), Report.Findings.Num(), 1);
		TestEqual(TEXT("and it is an error"), Report.ErrorCount, 1);
		TestEqual(TEXT("so the verdict is fail"), Report.Verdict, EMapVerdict::Fail);
		TestEqual(TEXT("and nothing was excluded"), Report.ExcludedCount, 0);
	}

	// With the tag exempted: the same finding, still printed, no longer an error, and counted.
	{
		Rules.bUseExemptions = true;
		Rules.ExemptTags.Add(TEXT("MapWarden.Ignore"));

		const FMapWardenReport Report = UMapWardenStatics::Analyze({ Broken }, Rules, Context());

		TestEqual(TEXT("the finding is still made"), Report.Findings.Num(), 1);
		TestEqual(TEXT("it is no longer an error"), Report.ErrorCount, 0);
		TestEqual(TEXT("it is info"), Report.InfoCount, 1);
		TestEqual(TEXT("it is counted as excluded"), Report.ExcludedCount, 1);
		TestTrue(TEXT("and it says so on the finding itself"), Report.Findings[0].bExcluded);
		TestEqual(TEXT("so the verdict is ok"), Report.Verdict, EMapVerdict::Ok);
	}

	// The other two lists reach the same actor by other routes.
	{
		FMapWardenRules ByClass = Rules;
		ByClass.ExemptTags.Reset();
		ByClass.ExemptClasses.Add(TEXT("BP_Door"));
		TestTrue(TEXT("the class list matches"), UMapWardenStatics::IsExempt(Broken, ByClass));

		FMapWardenRules ByBaseClass = Rules;
		ByBaseClass.ExemptTags.Reset();
		ByBaseClass.ExemptClasses.Add(TEXT("AActor"));
		TestTrue(TEXT("a base class in the chain matches, with or without the A prefix"),
			UMapWardenStatics::IsExempt(Broken, ByBaseClass));

		FMapWardenActorInfo InFolder = Broken;
		InFolder.Tags.Reset();
		InFolder.FolderPath = TEXT("Debug/Volumes");

		FMapWardenRules ByFolder = Rules;
		ByFolder.ExemptTags.Reset();
		ByFolder.ExemptFolders.Add(TEXT("Debug"));
		TestTrue(TEXT("the folder list matches as a prefix"), UMapWardenStatics::IsExempt(InFolder, ByFolder));

		FMapWardenRules Unrelated = Rules;
		Unrelated.ExemptTags.Reset();
		Unrelated.ExemptFolders.Add(TEXT("Debugging"));
		TestFalse(TEXT("but only on a whole folder name, not on any old prefix"),
			UMapWardenStatics::IsExempt(InFolder, Unrelated));
	}

	return true;
}

//
// (4) Judge returns Fail only when there is at least one Error.
//
// The three return codes are the interface between this plugin and somebody's build server. If a pile of
// warnings could ever add up to a failure, every project would have to argue with the gate instead of
// fixing the level.
//
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMapWardenJudgeTest,
	"MapWarden.Rules.JudgeFailsOnlyOnAnError",
	MapWardenTests::TestFlags)

bool FMapWardenJudgeTest::RunTest(const FString& Parameters)
{
	auto Finding = [](const EMapSeverity Severity)
	{
		FMapWardenFinding F;
		F.Severity = Severity;
		return F;
	};

	TestEqual(TEXT("nothing at all is ok"),
		UMapWardenStatics::Judge(TArray<FMapWardenFinding>()), EMapVerdict::Ok);

	TestEqual(TEXT("info only is ok"),
		UMapWardenStatics::Judge({ Finding(EMapSeverity::Info), Finding(EMapSeverity::Info) }), EMapVerdict::Ok);

	TestEqual(TEXT("a warning is warn"),
		UMapWardenStatics::Judge({ Finding(EMapSeverity::Info), Finding(EMapSeverity::Warning) }), EMapVerdict::Warn);

	// Twenty warnings are still warnings. This is the line that makes the gate predictable.
	TArray<FMapWardenFinding> ManyWarnings;
	for (int32 Index = 0; Index < 20; ++Index)
	{
		ManyWarnings.Add(Finding(EMapSeverity::Warning));
	}
	TestEqual(TEXT("twenty warnings are still only warn"),
		UMapWardenStatics::Judge(ManyWarnings), EMapVerdict::Warn);

	TestEqual(TEXT("one error among warnings is fail"),
		UMapWardenStatics::Judge({ Finding(EMapSeverity::Warning), Finding(EMapSeverity::Error) }), EMapVerdict::Fail);

	TestEqual(TEXT("ok exits 0"), UMapWardenStatics::VerdictExitCode(EMapVerdict::Ok), 0);
	TestEqual(TEXT("warn exits 1"), UMapWardenStatics::VerdictExitCode(EMapVerdict::Warn), 1);
	TestEqual(TEXT("fail exits 2"), UMapWardenStatics::VerdictExitCode(EMapVerdict::Fail), 2);

	return true;
}

//
// (5) The silent trigger is a warning, and cannot be made an error.
//
// Overlap delegates can be bound at runtime, from a Blueprint this scan never loaded. MapWarden cannot see
// that, says so on the finding, and refuses to fail a build over a suspicion - even when the settings ask it
// to. A checker that fails a build over something it admits it cannot see is a checker that gets deleted.
//
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMapWardenSilentTriggerTest,
	"MapWarden.Rules.SilentTriggerIsAlwaysAWarningNeverAnError",
	MapWardenTests::TestFlags)

bool FMapWardenSilentTriggerTest::RunTest(const FString& Parameters)
{
	using namespace MapWardenTests;

	FMapWardenActorInfo Trigger = Actor(TEXT("TriggerVolume_1"), TEXT("TriggerVolume"), FVector::ZeroVector);
	Trigger.bIsOverlapTrigger = true;

	FMapWardenRules Rules = NoChecks();
	Rules.bCheckSilentTriggers = true;

	// Even asked for an error, in as many words.
	Rules.SilentTriggerSeverity = EMapSeverity::Error;

	const FMapWardenReport Report = UMapWardenStatics::Analyze({ Trigger }, Rules, Context());

	TestEqual(TEXT("the silent trigger is found"), CountOfKind(Report.Findings, EMapFindingKind::SilentTrigger), 1);
	TestEqual(TEXT("no errors, whatever the settings said"), Report.ErrorCount, 0);
	TestEqual(TEXT("it is a warning"), Report.WarningCount, 1);
	TestEqual(TEXT("so the gate warns rather than fails"), Report.Verdict, EMapVerdict::Warn);

	// And the sentence has to admit what it cannot see, or the warning is dishonest.
	TestTrue(TEXT("the sentence says runtime bindings are invisible to it"),
		Report.Findings[0].Detail.Contains(TEXT("runtime")));

	// A bound delegate, or a level blueprint reference, and there is nothing to say at all.
	{
		FMapWardenActorInfo Bound = Trigger;
		Bound.bHasOverlapBinding = true;
		TestEqual(TEXT("a bound overlap delegate is not a finding"),
			UMapWardenStatics::Analyze({ Bound }, Rules, Context()).Findings.Num(), 0);

		FMapWardenActorInfo Referenced = Trigger;
		Referenced.bReferencedByLevelBlueprint = true;
		TestEqual(TEXT("a level blueprint reference is not a finding"),
			UMapWardenStatics::Analyze({ Referenced }, Rules, Context()).Findings.Num(), 0);
	}

	return true;
}

//
// (6) The grid finds exactly what the naive comparison finds, over five thousand actors.
//
// The duplicate search is the only part of this plugin that would be naively quadratic, and it is done on a
// grid to keep it linear. That is a performance trick, and every performance trick is a chance to be quietly
// wrong: a fast search that misses one pair in ten thousand is worse than a slow one, because the report
// will be green and nobody will know why the level got heavier.
//
// So the two are compared, pair for pair, on the same five thousand actors.
//
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMapWardenDuplicateGridTest,
	"MapWarden.Rules.DuplicateGridAgreesWithTheNaiveSearch",
	MapWardenTests::TestFlags)

bool FMapWardenDuplicateGridTest::RunTest(const FString& Parameters)
{
	using namespace MapWardenTests;

	FMapWardenRules Rules = NoChecks();
	Rules.bCheckDuplicates = true;

	TArray<FMapWardenActorInfo> Actors;
	Actors.Reserve(5000);

	FRandom Random;

	// Four thousand actors scattered over a hundred metres, which at a one-centimetre tolerance means the
	// grid has a great many mostly-empty cells and the occasional collision - the shape of a real level.
	for (int32 Index = 0; Index < 4000; ++Index)
	{
		const FVector Location(Random.Next(10000.0), Random.Next(10000.0), Random.Next(400.0));
		Actors.Add(Actor(*FString::Printf(TEXT("SM_Prop_%d"), Index), TEXT("StaticMeshActor"), Location));
	}

	// Then a thousand deliberate twins, offset by less than the tolerance in a direction that regularly puts
	// them in a neighbouring cell - which is the case a grid gets wrong if it only looks in its own cell.
	const int32 Placed = Actors.Num();
	for (int32 Index = 0; Index < 1000; ++Index)
	{
		const FMapWardenActorInfo& Source = Actors[Index * 3 % Placed];

		FMapWardenActorInfo Twin = Source;
		Twin.Name = FName(*FString::Printf(TEXT("SM_Prop_Twin_%d"), Index));
		Twin.Label = Twin.Name.ToString();
		Twin.Location += FVector(Random.Next(0.6), Random.Next(0.6), 0.0);
		Actors.Add(MoveTemp(Twin));
	}

	TestEqual(TEXT("five thousand actors"), Actors.Num(), 5000);

	TArray<TPair<int32, int32>> FromGrid;
	TArray<TPair<int32, int32>> FromNaive;

	UMapWardenStatics::FindDuplicatePairs(Actors, Rules, FromGrid);
	UMapWardenStatics::FindDuplicatePairsNaive(Actors, Rules, FromNaive);

	// Order is an implementation detail; the set of pairs is not.
	FromGrid.Sort([](const TPair<int32, int32>& A, const TPair<int32, int32>& B)
	{
		return (A.Key != B.Key) ? (A.Key < B.Key) : (A.Value < B.Value);
	});
	FromNaive.Sort([](const TPair<int32, int32>& A, const TPair<int32, int32>& B)
	{
		return (A.Key != B.Key) ? (A.Key < B.Key) : (A.Value < B.Value);
	});

	TestTrue(TEXT("the test data actually contains duplicates, or this test proves nothing"),
		FromNaive.Num() >= 1000);

	if (!TestEqual(TEXT("the grid and the naive search find the same number of pairs"),
		FromGrid.Num(), FromNaive.Num()))
	{
		return false;
	}

	for (int32 Index = 0; Index < FromNaive.Num(); ++Index)
	{
		if (FromGrid[Index] != FromNaive[Index])
		{
			AddError(FString::Printf(TEXT("pair %d differs: grid (%d,%d), naive (%d,%d)"),
				Index, FromGrid[Index].Key, FromGrid[Index].Value, FromNaive[Index].Key, FromNaive[Index].Value));
			return false;
		}
	}

	// And each pair is reported once. Reporting both directions would double every count on the report.
	TestEqual(TEXT("every pair is reported once, lower index first"),
		FromGrid.Num(), TSet<TPair<int32, int32>>(FromGrid).Num());

	return true;
}

//
// (7) The collision check only fires on a mesh that actually has collision to throw away.
//
// The reverse - a mesh with no collision model at all - is deliberately not a finding, because that is a
// decision the person who built the mesh took and not the person who placed it. This test is what stops
// somebody "improving" that later.
//
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMapWardenCollisionTest,
	"MapWarden.Rules.CollisionFindingNeedsAMeshThatHasCollision",
	MapWardenTests::TestFlags)

bool FMapWardenCollisionTest::RunTest(const FString& Parameters)
{
	using namespace MapWardenTests;

	FMapWardenRules Rules = NoChecks();
	Rules.bCheckMissingCollision = true;

	FMapWardenActorInfo Thrown = Actor(TEXT("SM_Crate_1"), TEXT("StaticMeshActor"), FVector::ZeroVector);
	Thrown.bHasStaticMesh = true;
	Thrown.MeshName = TEXT("SM_Crate");
	Thrown.bMeshHasCollisionModel = true;
	Thrown.bCollisionDisabled = true;

	TestEqual(TEXT("collision switched off on a mesh that has collision is a finding"),
		UMapWardenStatics::Analyze({ Thrown }, Rules, Context()).Findings.Num(), 1);

	FMapWardenActorInfo NeverHadAny = Thrown;
	NeverHadAny.bMeshHasCollisionModel = false;

	TestEqual(TEXT("a mesh with no collision model at all is not a finding, in either direction"),
		UMapWardenStatics::Analyze({ NeverHadAny }, Rules, Context()).Findings.Num(), 0);

	FMapWardenActorInfo Fine = Thrown;
	Fine.bCollisionDisabled = false;

	TestEqual(TEXT("collision left on is not a finding"),
		UMapWardenStatics::Analyze({ Fine }, Rules, Context()).Findings.Num(), 0);

	return true;
}

//
// (8) Mobility only fires when nothing at all moves the actor.
//
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMapWardenMobilityTest,
	"MapWarden.Rules.MobilityAcceptsEveryReasonToBeMovable",
	MapWardenTests::TestFlags)

bool FMapWardenMobilityTest::RunTest(const FString& Parameters)
{
	using namespace MapWardenTests;

	FMapWardenRules Rules = NoChecks();
	Rules.bCheckMobility = true;

	FMapWardenActorInfo Light = Actor(TEXT("PointLight_1"), TEXT("PointLight"), FVector::ZeroVector);
	Light.bHasLight = true;
	Light.bMovable = true;

	TestEqual(TEXT("a Movable light that nothing moves is a finding"),
		UMapWardenStatics::Analyze({ Light }, Rules, Context()).Findings.Num(), 1);

	// The four ways out, one at a time. Each of them is a real reason to be Movable, and getting any of them
	// wrong turns this check into noise on somebody's working level.
	{
		FMapWardenActorInfo Moved = Light;
		Moved.bHasMovementComponent = true;
		TestEqual(TEXT("a movement component excuses it"),
			UMapWardenStatics::Analyze({ Moved }, Rules, Context()).Findings.Num(), 0);
	}
	{
		FMapWardenActorInfo Simulating = Light;
		Simulating.bSimulatesPhysics = true;
		TestEqual(TEXT("a simulating body excuses it"),
			UMapWardenStatics::Analyze({ Simulating }, Rules, Context()).Findings.Num(), 0);
	}
	{
		FMapWardenActorInfo Attached = Light;
		Attached.bAttachedToMovable = true;
		TestEqual(TEXT("being bolted to something movable excuses it"),
			UMapWardenStatics::Analyze({ Attached }, Rules, Context()).Findings.Num(), 0);
	}
	{
		FMapWardenActorInfo Animated = Light;
		Animated.bReferencedBySequence = true;
		TestEqual(TEXT("being referenced by a level sequence excuses it"),
			UMapWardenStatics::Analyze({ Animated }, Rules, Context()).Findings.Num(), 0);
	}

	// And a Movable actor that is neither a light nor a mesh costs no shadow work, so there is nothing to say.
	{
		FMapWardenActorInfo Neither = Actor(TEXT("BP_Marker_1"), TEXT("BP_Marker"), FVector::ZeroVector);
		Neither.bMovable = true;
		TestEqual(TEXT("a Movable actor with neither a light nor a mesh is not a finding"),
			UMapWardenStatics::Analyze({ Neither }, Rules, Context()).Findings.Num(), 0);
	}

	return true;
}

//
// (9) The report says what it looked at, in numbers that match what it was given.
//
// This is the test behind the promise that "clean" can never mean "nobody looked": the headline has to carry
// the number of actors, and a partitioned world has to carry the sentence about unloaded cells.
//
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMapWardenCoverageTest,
	"MapWarden.Report.CoverageIsAlwaysStated",
	MapWardenTests::TestFlags)

bool FMapWardenCoverageTest::RunTest(const FString& Parameters)
{
	using namespace MapWardenTests;

	TArray<FMapWardenActorInfo> Actors;
	for (int32 Index = 0; Index < 4812; ++Index)
	{
		Actors.Add(Actor(*FString::Printf(TEXT("SM_Prop_%d"), Index), TEXT("StaticMeshActor"),
			FVector(static_cast<double>(Index) * 200.0, 0.0, 0.0)));
	}

	FMapWardenScanContext Ctx = Context();
	Ctx.LevelName = TEXT("L_Demo");
	Ctx.ActorsSkipped = 7;

	FMapWardenRules Rules = NoChecks();

	const FMapWardenReport Report = UMapWardenStatics::Analyze(Actors, Rules, Ctx);

	TestEqual(TEXT("every actor handed in was checked"), Report.ActorsChecked, 4812);

	const FString Headline = UMapWardenStatics::FormatHeadline(Report);
	TestTrue(TEXT("the headline carries the count, grouped"), Headline.Contains(TEXT("4,812")));
	TestTrue(TEXT("the headline names the level"), Headline.Contains(TEXT("L_Demo")));
	TestTrue(TEXT("the headline carries the excluded counter even when it is zero"),
		Headline.Contains(TEXT("0 excluded by settings")));

	const FString Coverage = UMapWardenStatics::FormatCoverage(Report);
	TestTrue(TEXT("the coverage line admits what was skipped"), Coverage.Contains(TEXT("7 not checked")));

	// The partitioned case, which is the one that matters.
	FMapWardenScanContext Partitioned = Ctx;
	Partitioned.bWorldPartition = true;
	Partitioned.LoadedLevels = { TEXT("Cell_0_0"), TEXT("Cell_1_0") };

	const FMapWardenReport PartitionedReport = UMapWardenStatics::Analyze(Actors, Rules, Partitioned);
	TestTrue(TEXT("a partitioned world says unloaded cells were not checked"),
		UMapWardenStatics::FormatCoverage(PartitionedReport).Contains(TEXT("UNLOADED CELLS WERE NOT CHECKED")));

	// A report nobody has run is not a clean report, and must not read like one.
	const FMapWardenReport NeverRun;
	TestTrue(TEXT("an unrun report says so"),
		UMapWardenStatics::FormatHeadline(NeverRun).Contains(TEXT("NOT RUN")));

	// Grouping, since a screenshot of the headline is the sales picture and it has to read the same
	// everywhere.
	TestEqual(TEXT("0"), UMapWardenStatics::FormatCount(0), FString(TEXT("0")));
	TestEqual(TEXT("999"), UMapWardenStatics::FormatCount(999), FString(TEXT("999")));
	TestEqual(TEXT("1,000"), UMapWardenStatics::FormatCount(1000), FString(TEXT("1,000")));
	TestEqual(TEXT("1,234,567"), UMapWardenStatics::FormatCount(1234567), FString(TEXT("1,234,567")));

	return true;
}

//
// (10) Every finding ends in a sentence, and the ordering of the report is stable.
//
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMapWardenFindingOrderTest,
	"MapWarden.Report.FindingsAreExplainedAndOrdered",
	MapWardenTests::TestFlags)

bool FMapWardenFindingOrderTest::RunTest(const FString& Parameters)
{
	using namespace MapWardenTests;

	FMapWardenRules Rules;
	Rules.bUseExemptions = false;

	// Chosen so that only the lost cube is outside it. The centre is the average of the four, which the cube
	// itself drags a long way out - a reminder that the bounds check measures against where the level
	// actually is and not against the origin.
	Rules.OutOfBoundsRadiusCm = 200000.0f;

	// A duplicate pair (warning), a silent trigger (warning) and an actor far outside (error).
	FMapWardenActorInfo First = Actor(TEXT("SM_Crate_1"), TEXT("StaticMeshActor"), FVector::ZeroVector);
	FMapWardenActorInfo Second = Actor(TEXT("SM_Crate_2"), TEXT("StaticMeshActor"), FVector(0.5, 0.0, 0.0));

	FMapWardenActorInfo Trigger = Actor(TEXT("TriggerVolume_1"), TEXT("TriggerVolume"), FVector::ZeroVector);
	Trigger.bIsOverlapTrigger = true;

	FMapWardenActorInfo Lost = Actor(TEXT("SM_Cube_9"), TEXT("StaticMeshActor"), FVector(500000.0, 0.0, 0.0));

	const FMapWardenReport Report = UMapWardenStatics::Analyze({ First, Second, Trigger, Lost }, Rules, Context());

	TestTrue(TEXT("something was found"), Report.Findings.Num() >= 3);
	TestEqual(TEXT("the lost cube is the error"), Report.ErrorCount, 1);
	TestEqual(TEXT("errors come first"), Report.Findings[0].Severity, EMapSeverity::Error);
	TestEqual(TEXT("and the first finding is the one that is out of bounds"),
		Report.Findings[0].Kind, EMapFindingKind::OutOfBounds);

	for (const FMapWardenFinding& Finding : Report.Findings)
	{
		if (Finding.Detail.IsEmpty())
		{
			AddError(FString::Printf(TEXT("finding about %s has no sentence"), *Finding.ActorName.ToString()));
		}

		// A line without the actor's name is a line somebody has to go hunting after.
		if (!UMapWardenStatics::FormatFinding(Finding).Contains(Finding.ActorName.ToString()))
		{
			AddError(FString::Printf(TEXT("the line for %s does not name it"), *Finding.ActorName.ToString()));
		}
	}

	// Run twice over the same level: the same report, in the same order.
	const FMapWardenReport Again = UMapWardenStatics::Analyze({ First, Second, Trigger, Lost }, Rules, Context());
	TestEqual(TEXT("the same level checked twice produces the same number of findings"),
		Again.Findings.Num(), Report.Findings.Num());

	for (int32 Index = 0; Index < Report.Findings.Num(); ++Index)
	{
		TestEqual(TEXT("in the same order"),
			Again.Findings[Index].ActorName.ToString(), Report.Findings[Index].ActorName.ToString());
	}

	// And the JSON carries the two fields a build script actually reads.
	const FString Json = UMapWardenStatics::ReportToJson(Report);
	TestTrue(TEXT("the JSON carries the verdict"), Json.Contains(TEXT("\"verdict\"")));
	TestTrue(TEXT("the JSON carries the exit code"), Json.Contains(TEXT("\"exitCode\"")));
	TestTrue(TEXT("and it carries the coverage block"), Json.Contains(TEXT("\"coverage\"")));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
