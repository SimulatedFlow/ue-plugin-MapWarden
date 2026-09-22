# MapWarden — Documentation

**Online:
[wiki.teufel-engineering.com/en/MapWarden/documentation](https://wiki.teufel-engineering.com/en/MapWarden/documentation)**

MapWarden checks what is actually **placed in a level**. It is a runtime plugin with an editor front end, a
Blueprint API, a C++ API and a build gate, for **Unreal Engine 5.8** on **Win64**.

---

## Contents

1. [Supported engine and platforms](#supported-engine-and-platforms)
2. [Installing](#installing)
3. [Quick start](#quick-start)
4. [The seven checks](#the-seven-checks)
5. [The report](#the-report)
6. [Exemptions](#exemptions)
7. [What MapWarden cannot know](#what-mapwarden-cannot-know)
8. [World Partition: what was checked and what was not](#world-partition-what-was-checked-and-what-was-not)
9. [MapWarden against AssetWarden — which one for what](#mapwarden-against-assetwarden--which-one-for-what)
10. [The gate and the JSON report](#the-gate-and-the-json-report)
11. [Console commands](#console-commands)
12. [Class and API overview](#class-and-api-overview)
13. [Code examples](#code-examples)
14. [Blueprint API](#blueprint-api)
15. [The demo map](#the-demo-map)
16. [Settings reference](#settings-reference)
17. [Architecture](#architecture)
18. [Tests](#tests)
19. [Troubleshooting](#troubleshooting)
20. [Support](#support)

---

## Supported engine and platforms

| | |
|---|---|
| **Engine version** | Unreal Engine **5.8** (`"EngineVersion": "5.8.0"`) |
| **Supported development platforms** | Windows (**Win64**) |
| **Supported target build platforms** | Windows (**Win64**) |
| **Plugin type** | Code plugin, full C++ source included |
| **Modules** | `MapWarden` (Runtime, `PreDefault`), `MapWardenEditor` (Editor, `PostEngineInit`) — both `PlatformAllowList: ["Win64"]` |
| **Engine dependencies** | `Core`, `CoreUObject`, `Engine`, `DeveloperSettings`, `RenderCore`, `Json`, `JsonUtilities` (runtime) · `UnrealEd`, `Slate`, `SlateCore`, `LevelEditor` (editor) |
| **Third-party code** | None |
| **Other plugin dependencies** | None |
| **Network replicated** | No — MapWarden is a local diagnostic |
| **Blueprint support** | Yes, full: every rule, the report and the gate are exposed |
| **Works in a cooked/Shipping build** | Yes. The report is drawn on `UCanvas` from an `AHUD`, so it survives cooking. The editor menu and "select that actor" are editor-only, by design. |

Both modules are Win64-only because that is the platform the plugin is built and verified for. The runtime
module contains nothing platform-specific; if you need another target platform, the source is in the box.

---

## Installing

1. Copy the `MapWarden` folder into your project's `Plugins` folder, so you end up with
   `<YourProject>/Plugins/MapWarden/MapWarden.uplugin`.
2. Open the project. A C++ project builds the plugin with it. A Blueprint-only project will offer to rebuild
   — accept, or add a C++ class once so the project has a solution.
3. Confirm the plugin is enabled under **Edit → Plugins → Engine Tools → MapWarden** (it is enabled by
   default when it sits in `Plugins`).
4. Configure everything under **Project Settings → Plugins → MapWarden**. The settings are `config = Game,
   defaultconfig`, so they land in your project's `Config/DefaultGame.ini` and travel with the project.

To call MapWarden from C++, add the module to your own module's `Build.cs`:

```csharp
PublicDependencyModuleNames.AddRange(new string[] { "MapWarden" });
```

There is nothing else to wire up. The console commands, the auto-scan and the editor menu register
themselves.

The runtime module does not depend on the editor module. That is what lets `MapWarden.Gate` run in a
commandlet with no editor, and what lets the report be drawn in a cooked Shipping build.

---

## Quick start

### In the editor, in ten seconds

Open a level and run **Tools → MapWarden → Check This Level**. The headline arrives as a toast, and the
whole report goes to the Output Log:

```
MapWarden FAIL | actors 28 checked in L_MapWardenDemo | errors 1  warnings 6  info 1 | 1 excluded by settings | scan 0 ms
```

*(That line is the real output of the shipped demo map, `L_MapWardenDemo`, in play-in-editor with the default
settings — not an illustration.)*

Then, in order of usefulness:

* **Tools → MapWarden → Select Everything With A Finding** selects every actor a finding is about, so you
  see the whole list at once in the viewport and the outliner.
* **Tools → MapWarden → Write Level Report** writes the same JSON your build server will read.

### In a running game

Give your game mode `AMapWardenHUD` as its HUD class (or turn on **Auto Draw On Any HUD** and keep your
own). Press Play. One second after Begin Play the level is checked and the panel is drawn on screen.

The one-second wait is not laziness: actors bind their overlap delegates in `BeginPlay`, and a check in the
same frame as the map load would report every trigger in the level as silent.

### On a build server

```
MapWarden.Gate
```

Writes `Saved/MapWarden/report.json` and ends the process with **0** (clean), **1** (warnings) or **2**
(errors).

---

## The seven checks

Every check has its own switch, its own threshold and its own severity. A tool with one fixed opinion gets
switched off on the first run against a real level; that is why none of this is hard-wired.

### 1. Duplicates

Two actors **of the same class**, within `Duplicate Tolerance Cm` of each other (default 1 cm), with
rotations within `Duplicate Rotation Tolerance Deg` (default 1°) and scales within
`Duplicate Scale Tolerance` (default 1%).

All four conditions have to hold. Two crates a millimetre apart and turned ninety degrees are a wall
somebody built on purpose; two crates a millimetre apart facing the same way are a `Ctrl+D` nobody moved.
Without the rotation and scale test, every tiled floor in your project would be reported.

Exactly at the tolerance counts as a duplicate — the boundary is inside the tolerance, and it is tested.

**The search is on a grid.** This is the one part of MapWarden that would be naively quadratic: twenty
thousand actors compared against each other is two hundred million distance tests. The actors go into a
uniform grid whose cell size *is* the tolerance, so two actors closer than the tolerance are by construction
in the same cell or in one of the twenty-six around it, and each actor is only ever compared against its own
neighbourhood. One of the automation tests runs both the grid and a naive all-pairs comparison over five
thousand actors and requires them to produce identical pairs.

The finding is reported against the **second** actor of the pair, because the first is usually the one that
was there before somebody pressed `Ctrl+D`.

### 2. Collision missing where it was expected

A `UStaticMeshComponent` set to `No Collision` whose `UStaticMesh` **does** have simple collision on its
body setup.

That is the whole rule, and the reverse is deliberately not a finding: a mesh with no collision model at all
is a decision the person who built the mesh took, not the person who placed it. MapWarden reports the case
where somebody's collision work is being thrown away by a placement, and nothing else.

### 3. Mobility

A light or static mesh component set to `Movable` on an actor where **none** of the following is true:

* the actor has a `UMovementComponent`;
* a primitive on it is set to simulate physics;
* it is attached to an actor whose root is not `Static`;
* it is referenced by a level sequence actor standing in this level.

All four are real reasons to be Movable. Get any of them wrong and the check becomes noise on a working
level, so all four are tested.

The sequence test is the honest limit here: MapWarden takes no dependency on MovieScene, so it can see that
a level sequence actor **references** an actor, but not that a sequence possesses it through a binding
stored in the sequence asset. If your animation is driven that way, use the exemption list — that is what it
is for.

### 4. Out of bounds

An actor further than `Out Of Bounds Radius Cm` (default 100 000 cm) from the **centre of everything that
was checked**.

The centre is the average position of the checked actors, not the world origin. A level built two kilometres
from the origin is an ordinary level, and measuring from the origin would report all of it.

Exactly on the radius is inside it.

### 5. Silent triggers

An actor with a primitive component that generates overlap events and has query collision, where nothing was
found listening: no bound `OnActorBeginOverlap` / `OnActorEndOverlap`, no bound `OnComponentBeginOverlap` /
`OnComponentEndOverlap`, and no reference from the level blueprint.

**This finding is clamped to Warning and can never be an error**, whatever the severity setting says. A
binding can be made at runtime, from a Blueprint this scan never loaded, and a check that admits it cannot
see something must not be allowed to fail somebody's build over it. The sentence on the finding says so in
words, and one of the automation tests asserts the clamp.

The check is much more accurate in a running game than in the editor, because in the editor no `BeginPlay`
has run and almost nothing is bound yet. If you use it in the editor, treat it as "which of these volumes is
not referenced by the level blueprint" and read the sentence.

### 6. Dead references

Soft object references — on the actor or on any of its components, including inside structs and arrays —
whose target no longer resolves.

The order is: already loaded? Then it is alive. Otherwise, does the package exist at all? That is the case
that actually happens: somebody deleted the asset and the redirector went with it. With
`Resolve Soft References` turned on, MapWarden also loads the package to check the object inside it is still
there, which catches a renamed object in a surviving package and costs disk on every scan — which is why it
is off by default.

`/Script/...` paths and malformed paths are not reported. A class reference that does not resolve is a
missing module, not a broken level.

Hard `UObject*` references are **not** checked, and that is deliberate: a null hard reference is
indistinguishable from one nobody ever set, and MapWarden does not report things it cannot prove.

### 7. Default names

More than `Default Name Limit` actors (default 50) whose label is still the one their class handed out —
`StaticMeshActor_231`, `PointLight_4`.

One finding about the level, not one per actor. **Off by default**: nothing is broken, it is a level nobody
can navigate in the outliner, and how much of that you can live with is taste.

---

## The report

Drawn on `UCanvas` from an `AHUD`, which is why it survives a cooked Shipping build. Set `AMapWardenHUD` as
the HUD class on your game mode, or leave your own HUD alone and turn on **Auto Draw On Any HUD**, which
routes the identical panel through `AHUD::OnHUDPostRender`. The two paths know about each other and cannot
draw twice.

The first line is the verdict and the arithmetic behind it. The second is coverage — how much of the level
this report is actually about. Then the findings, errors first, each one naming the actor, what was measured
and the threshold it broke.

This is the whole panel from the shipped demo map, verbatim:

```
MapWarden FAIL | actors 28 checked in L_MapWardenDemo | errors 1  warnings 6  info 1 | 1 excluded by settings | scan 0 ms
28 actors checked in 1 level(s); 15 not checked (spawned at runtime, editor-only or on the ignore list); source play in editor
error   bounds     LostCube is 252,556 cm from the centre of the level (limit 100,000 cm)
warning duplicate  Crate_A_Copy sits 0.00 cm from Crate_A, same rotation and scale (tolerance 1.00 cm)
warning duplicate  Crate_B_Copy sits 0.00 cm from Crate_B, same rotation and scale (tolerance 1.00 cm)
warning collision  GlassPane is set to No Collision but Cube has a collision model
warning mobility   Pallet is a Movable mesh that nothing moves
warning mobility   WorkLight_Movable is a Movable light that nothing moves
warning trigger    LoadingBayTrigger overlaps, and nothing is listening (runtime bindings are not visible here)
info    collision  DecorCrate is set to No Collision but Cube has a collision model   [excluded by settings]
```

Note the last line. `DecorCrate` carries an exempt tag, so its finding was made, forced down to `Info`,
counted in `1 excluded by settings` — and still printed with the actor's name.

When there is nothing to report, the panel turns green **and lists what it checked**, with any switched-off
check printed in warning colour. A checker that goes quiet when it is happy is indistinguishable from a
checker that never ran, and that is how a broken gate stays broken for a milestone.

`Max Report Rows` limits how many findings the panel lists; the rest are counted and `MapWarden.Dump` writes
all of them to the log.

---

## Exemptions

Three lists, in Project Settings:

* **Exempt Classes** — matched against the actor's whole class chain, so naming a base class exempts every
  Blueprint derived from it. `StaticMeshActor` and `AStaticMeshActor` both work.
* **Exempt Folders** — matched as a prefix on the outliner folder, so `Debug` also covers `Debug/Volumes`,
  but not `Debugging`.
* **Exempt Tags** — actor tags. The only exemption a level designer can apply without leaving the level.

An exempt actor is **not skipped**. Its findings are made, forced down to `Info`, counted as
`excluded by settings` and still printed with the actor's name. An exemption list that could hide its own
effect would be a way to turn a level green by editing a settings page, which is precisely what a gate
exists to prevent.

`MapWarden.Exempt 0` shows you the same level with the lists switched off, for one session, without touching
a config file. On the demo map that is the difference between

```
errors 1  warnings 6  info 1 | 1 excluded by settings     (exemptions on)
errors 1  warnings 7  info 0 | 0 excluded by settings     (exemptions off)
```

— the exempted `DecorCrate` collision finding moving from `info` back up to `warning`. Both lines are
measured, not illustrative.

Separately from exemptions there is **Ignored Classes**, which are never checked at all: engine bookkeeping
that lives in every level and belongs to nobody (`WorldSettings`, `LevelScriptActor`, `AbstractNavData`).
Ignored actors are counted in the "not checked" number on the coverage line. Note that `Brush` is
deliberately *not* on that list — `AVolume` derives from `ABrush`, and ignoring brushes would quietly switch
the silent-trigger check off for every trigger volume in your project.

---

## What MapWarden cannot know

Two things, stated here and on the report rather than buried:

**It cannot know what you meant.** A mesh with collision switched off may be decoration behind glass. A
Movable light may be moved by code MapWarden never sees. A trigger may be bound in `BeginPlay` by a
Blueprint that has not loaded yet. Every one of those is a legitimate level, and none of them is
distinguishable from a mistake by looking at the level. That is why every check is switchable, every
threshold is yours, the exemption lists exist — and why what they exempt is still counted and shown.

**It checks the map that is loaded.** MapWarden walks the actors that are in memory. Anything not loaded was
not checked, and the report says how many actors and which levels it saw.

---

## World Partition: what was checked and what was not

On a partitioned world, only the cells that are loaded exist in memory. Everything else is on disk and
MapWarden has not looked at it.

So on a partitioned world the coverage line reads:

```
World Partition: <n> actors in <n> loaded cell(s) checked - UNLOADED CELLS WERE NOT CHECKED
```

in warning colour, the log repeats it as a warning, and the JSON carries it as
`coverage.worldPartition: true` with `coverage.complete: false` and the list of levels that were in memory.
A build script can refuse a report that was not allowed to check what it claims to.

**How to get full coverage on a partitioned world**, in order of preference:

1. Load the cells you care about in the editor (or use a data layer that loads them) and run the check
   there. What is loaded is what is checked, and the report tells you what that was.
2. Run the check per region as part of a level-authoring pass rather than as a single all-or-nothing gate.
3. On a build server, run the gate against the levels you can load fully — the gate is per loaded world, so
   several runs over several worlds is a normal way to use it.

MapWarden will not pretend. "Clean" must never be allowed to mean "nobody looked", and on a partitioned
world that distinction is the difference between a report and a false certificate.

---

## MapWarden against AssetWarden — which one for what

They are in the same family, they return the same three exit codes, and they answer different questions.

| | AssetWarden | MapWarden |
|---|---|---|
| Subject | the **content folder** | the **map** |
| Asks | is this asset named right, in the right folder, pointing at things that exist | what is standing in this level, how many, where, with what settings |
| Runs over | the asset registry — everything on disk, loaded or not | the actors in memory in the loaded world |
| Typical finding | `T_crate_D` should be `T_Crate_D`; `MI_Wall` references a deleted material | `Crate_B_Copy` sits 0.00 cm from `Crate_B`; `WorkLight_Movable` is Movable and nothing moves it |
| Covers unloaded content | yes, that is its whole point | no — see above |

**What AssetWarden does better:** it sees your entire project without loading it, it enforces naming and
folder conventions, and it finds broken references in assets nobody has placed anywhere. If you can only run
one of the two on a build server, and your problem is content hygiene, run AssetWarden.

**What MapWarden does that AssetWarden cannot:** the same mesh can be flawless on disk and placed three
times in exactly the same spot. Placement, transforms, mobility, collision settings and overlap bindings do
not exist as questions about an asset — they only exist in a level.

Run both. They do not overlap.

---

## The gate and the JSON report

```
MapWarden.Gate [path] [-noexit]
```

Checks the loaded level, writes the JSON report and ends the process with:

| Exit code | Meaning |
|---|---|
| `0` | ok — nothing above Info |
| `1` | warn — at least one Warning, no Errors |
| `2` | fail — at least one Error |

The same three codes as **AssetWarden**, **LocaleGuard**, **BindGuard**, **ReadGuard**, **WidgetLedger**,
**LoadLens** and **HeapCensus**.

A report that could not be written, or a gate that found no loaded level at all, exits `2`. A gate that did
not run must never look like a gate that passed.

`-noexit` writes the report and prints the verdict without ending the process, which is what you want when
you are gating several maps from one editor session.

Running it from a commandlet with a map loaded:

```
UnrealEditor-Cmd.exe <Project>.uproject <MapPackage> -run=... -ExecCmds="MapWarden.Gate" -unattended -nosplash
```

Any route that loads a map and can execute a console command works; MapWarden picks the game world if there
is one, then play-in-editor, then the editor world.

The JSON below is the **real file** the demo map produced (`Saved/MapWarden/report.json`), abridged to one
finding:

```json
{
  "tool": "MapWarden",
  "version": "1.0.0",
  "verdict": "fail",
  "exitCode": 2,
  "level": "L_MapWardenDemo",
  "source": "play in editor",
  "actorsChecked": 28,
  "actorsSkipped": 15,
  "errors": 1,
  "warnings": 6,
  "info": 1,
  "excludedBySettings": 1,
  "scanMilliseconds": 0.4360005259513855,
  "coverage": {
    "worldPartition": false,
    "complete": true,
    "boundsRadiusCm": 100000,
    "summary": "28 actors checked in 1 level(s); 15 not checked (spawned at runtime, editor-only or on the ignore list); source play in editor",
    "loadedLevels": [ "UEDPIE_0_L_MapWardenDemo" ]
  },
  "checks": [
    "duplicates: same class, within 1.00 cm, 1.0 deg and 1% scale of each other",
    "collision: static meshes set to No Collision whose mesh does have a collision model",
    "mobility: Movable lights and meshes with no movement, no physics and no sequence",
    "bounds: further than 100000 cm from the centre of everything checked",
    "triggers: overlap volumes with no delegate bound and no level blueprint reference",
    "references: soft references on placed actors that no longer resolve",
    "names: NOT CHECKED (off by default - how many default names is taste)"
  ],
  "findings": [
    {
      "kind": "bounds",
      "severity": "error",
      "actor": "LostCube",
      "actorPath": "/MapWarden/MapWarden/Maps/L_MapWardenDemo.L_MapWardenDemo:PersistentLevel.StaticMeshActor_9",
      "class": "StaticMeshActor",
      "other": "None",
      "context": "",
      "measured": 252556.390625,
      "threshold": 100000,
      "unit": "cm",
      "excludedBySettings": false,
      "detail": "...",
      "location": { "x": 0.0, "y": 0.0, "z": 0.0 }
    }
  ]
}
```

The field names are a published interface: they are what build scripts grep, and they will not change
because a C++ member was renamed. Note `checks` — every scan writes down what it looked for, including what
it did **not** look for, so a green report can never be confused with a report that never ran.

A minimal CI step, in PowerShell:

```powershell
& "$Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$Project" "/Game/Maps/L_Level" `
    -ExecCmds="MapWarden.Gate" -unattended -nosplash -nullrhi
if ($LASTEXITCODE -ge 2) { throw "MapWarden: the level has errors. See Saved/MapWarden/report.json" }
if ($LASTEXITCODE -eq 1) { Write-Warning "MapWarden: warnings in the level." }
```

---

## Console commands

| Command | What it does |
|---|---|
| `MapWarden.Scan` | check the loaded level now, print the headline |
| `MapWarden.Dump` | the whole report to the log: every finding, its sentence, and what was checked |
| `MapWarden.Show [0\|1]` | show the on-screen panel |
| `MapWarden.Hide` | hide it |
| `MapWarden.Report [path]` | check, then write the JSON report |
| `MapWarden.Gate [path] [-noexit]` | check, write the report, exit 0 / 1 / 2 |
| `MapWarden.Exempt [0\|1]` | use the exemption lists, or do not, for this session — re-checks immediately |

`Scan`, `Dump`, `Report` and `Gate` work with a game running and without one. `Show` and `Hide` need a
running game — there is no HUD in the editor viewport; use `MapWarden.Dump` instead.

---

## Class and API overview

Five public classes plus two plain structs. Everything below is in module `MapWarden` unless marked.

### `UMapWardenSubsystem : UWorldSubsystem`
`MapWardenSubsystem.h` — the checker, one per world. A **world** subsystem and not a game-instance one,
because the subject is the level: two levels loaded one after the other are two different answers. It
supports editor worlds as well as game and PIE worlds, which is what lets **Tools → MapWarden** and the
commandlet gate use exactly the same code path.

| Member | Signature | Notes |
|---|---|---|
| `Get` | `static UMapWardenSubsystem* Get(const UObject* WorldContext)` | null when there is no world |
| `Scan` | `FMapWardenReport Scan()` | checks now, broadcasts `OnFindings`, returns the report |
| `GetReport` | `const FMapWardenReport& GetReport() const` | the last report, no new check |
| `GetFindings` | `TArray<FMapWardenFinding> GetFindings() const` | |
| `GetVerdict` | `EMapVerdict GetVerdict() const` | |
| `WriteReport` | `bool WriteReport(const FString& Path)` | empty path = the one in Project Settings; checks first if nothing has run |
| `FocusFinding` | `bool FocusFinding(int32 Index)` | selects the actor in the editor viewport; `false` outside the editor |
| `SetReportVisible` / `IsReportVisible` | | the on-screen panel |
| `SetExemptionsEnabled` / `AreExemptionsEnabled` | | session override; re-checks immediately |
| `DrawReport` | `void DrawReport(UCanvas*, const FVector2D& Origin, float Width) const` | called by the HUD |
| `OnFindings` | `FMapWardenFindingsSignature` (`BlueprintAssignable`) | fires after every check — how a UMG panel rebuilds without polling |

### `UMapWardenStatics : UBlueprintFunctionLibrary`
`MapWardenStatics.h` — **the rules**, and the Blueprint surface. Everything in the first group is a static
function over plain structs: no world, no subsystem, no actor. That is why the rules are covered by
automation tests and why you can call them from your own tooling.

*Rules:* `AreDuplicates`, `IsOutOfBounds`, `Judge`, `Explain`, `IsExempt`, `ComputeCentre`,
`FindDuplicatePairs` (grid), `FindDuplicatePairsNaive` (the reference the grid is tested against),
`Analyze` (run every enabled check over an array of actor infos).

*Formatting:* `VerdictName`, `VerdictExitCode`, `SeverityName`, `KindName`, `FormatCount`, `FormatHeadline`,
`FormatFinding`, `FormatCoverage`, `ReportToJson`.

*World access (Blueprint convenience over the subsystem):* `ScanNow`, `GetLastReport`, `GetFindings`,
`GetVerdict`, `WriteReport`, `FocusFinding`, `SetExemptionsEnabled`, `AreExemptionsEnabled`,
`SetReportVisible`, `IsReportVisible`.

### `AMapWardenHUD : AHUD`
`MapWardenHUD.h` — draws the report on `UCanvas`, which is why it works in a cooked Shipping build where a
UMG debug panel has usually been stripped. `PanelOrigin` (default `28, 90`) and `PanelWidth` (default `980`)
are editable. `ToggleReport()`, `IsReportVisible()`, `ScanNow()` are Blueprint-callable.

### `UMapWardenSettings : UDeveloperSettings`
`MapWardenSettings.h` — **Project Settings → Plugins → MapWarden**, `config = Game, defaultconfig`. Every
switch, every threshold, every severity, the three exemption lists, the scanning options and the report
options. `UMapWardenSettings::Get()` returns it, `MakeRules()` flattens it into `FMapWardenRules`.

### `FMapWardenScanner` (plain struct)
`MapWardenScanner.h` — **the only code in the plugin that reads a world.** `DescribeActor`, `GatherActors`,
`Run`, `RunWithProjectSettings`, `FindBestWorld` (game → PIE → editor), `WriteReportFile`, `LogReport`.

### `FMapWardenFocus` (plain struct)
`MapWardenFocus.h` — a `DECLARE_DELEGATE_RetVal_OneParam(bool, …, const FString& ActorPath)`. The runtime
module asks "show me that actor"; the editor module binds the answer at startup. In a cooked build nothing
is bound and `Focus` returns `false`, which is the correct answer rather than a degraded one.

### Types — `MapWardenTypes.h`

| Type | What it is |
|---|---|
| `EMapFindingKind` | `Duplicate`, `MissingCollision`, `Mobility`, `OutOfBounds`, `SilentTrigger`, `DeadReference`, `DefaultName` |
| `EMapSeverity` | `Info`, `Warning`, `Error` |
| `EMapVerdict` | `Ok`, `Warn`, `Fail` → exit codes 0, 1, 2 |
| `FMapWardenActorInfo` | everything the rules ever see about one placed actor: names, class chain, transform, folder, tags, and the booleans behind each check |
| `FMapWardenFinding` | kind, severity, actor name/path/class, location, the other actor, context, `MeasuredValue`, `Threshold`, `Unit`, `bExcluded`, and `Detail` — one sentence about what to do |
| `FMapWardenRules` | the settings, flattened. A test builds one in three lines |
| `FMapWardenScanContext` | what the scan looked at: level name, loaded levels, World Partition flag, actors skipped, source |
| `FMapWardenReport` | findings, counts, `ExcludedCount`, `ScanMilliseconds`, verdict, centre and radius, coverage, `ChecksRun`, `bHasRun` |

### `MapWardenEditor` module
Adds **Tools → MapWarden** (*Check This Level*, *Select Everything With A Finding*, *Write Level Report*)
and binds `FMapWardenFocus` so a finding can select its actor and move the viewport camera. The dependency
arrow points editor → runtime and never back.

---

## Code examples

### 1. Check the level from C++ and act on the verdict

```cpp
#include "MapWardenSubsystem.h"
#include "MapWardenStatics.h"

void AMyGameMode::CheckThisLevel()
{
    UMapWardenSubsystem* Warden = UMapWardenSubsystem::Get(this);
    if (!Warden)
    {
        return; // no world, nothing to check
    }

    const FMapWardenReport Report = Warden->Scan();

    UE_LOG(LogTemp, Display, TEXT("%s"), *UMapWardenStatics::FormatHeadline(Report));
    UE_LOG(LogTemp, Display, TEXT("%s"), *UMapWardenStatics::FormatCoverage(Report));

    for (const FMapWardenFinding& Finding : Report.Findings)
    {
        UE_LOG(LogTemp, Display, TEXT("  %s"), *UMapWardenStatics::FormatFinding(Finding));
        UE_LOG(LogTemp, Verbose,  TEXT("    -> %s"), *Finding.Detail);
    }

    if (Report.Verdict == EMapVerdict::Fail)
    {
        // 2. Errors. UMapWardenStatics::VerdictExitCode(Report.Verdict) is the number the gate returns.
    }
}
```

### 2. Rebuild a panel whenever a check finishes, without polling

```cpp
void UMyLevelToolsWidget::NativeConstruct()
{
    Super::NativeConstruct();

    if (UMapWardenSubsystem* Warden = UMapWardenSubsystem::Get(this))
    {
        Warden->OnFindings.AddDynamic(this, &UMyLevelToolsWidget::HandleFindings);
    }
}

void UMyLevelToolsWidget::HandleFindings(const FMapWardenReport& Report)
{
    HeadlineText->SetText(FText::FromString(UMapWardenStatics::FormatHeadline(Report)));
    ErrorCount = Report.ErrorCount;
    // Report.ExcludedCount is what the exemption lists forgave. Always show it.
}
```

### 3. Use the rules on your own data, with no world at all

This is the point of the pure-rules split: you can run MapWarden's logic over actors you describe yourself —
from a commandlet, from a test, from a tool that reads a level you have not loaded into a world.

```cpp
#include "MapWardenStatics.h"

FMapWardenActorInfo MakeCrate(const FName Name, const FVector Where)
{
    FMapWardenActorInfo Info;
    Info.Name       = Name;
    Info.ClassName  = TEXT("StaticMeshActor");
    Info.ClassChain = { TEXT("StaticMeshActor"), TEXT("Actor") };
    Info.Location   = Where;
    return Info;
}

void CheckTwoCrates()
{
    const FMapWardenActorInfo A = MakeCrate(TEXT("Crate_A"),      FVector(0, 0, 0));
    const FMapWardenActorInfo B = MakeCrate(TEXT("Crate_A_Copy"), FVector(0.5, 0, 0));

    // Same class, half a centimetre apart, same rotation and scale -> a Ctrl+D nobody moved.
    const bool bSame = UMapWardenStatics::AreDuplicates(A, B, /*ToleranceCm=*/1.0f,
                                                        /*RotationToleranceDeg=*/1.0f,
                                                        /*ScaleTolerance=*/0.01f);
    check(bSame);

    // The centre is the average of what was checked, never the world origin.
    const FVector Centre = UMapWardenStatics::ComputeCentre({ A, B });
    check(!UMapWardenStatics::IsOutOfBounds(B.Location, Centre, 100000.0f));
}
```

### 4. Run every check over an array you built yourself

```cpp
FMapWardenRules Rules;                      // defaults: six checks on, default names off
Rules.bCheckSilentTriggers = false;         // switch off exactly the one you do not want
Rules.OutOfBoundsRadiusCm  = 50000.0f;      // your threshold, in the unit you think in
Rules.ExemptTags.Add(TEXT("LevelArt.Decor"));

FMapWardenScanContext Context;
Context.LevelName  = TEXT("L_MyLevel");
Context.SourceName = TEXT("my own tool");

const FMapWardenReport Report = UMapWardenStatics::Analyze(Actors, Rules, Context);
const int32 ExitCode = UMapWardenStatics::VerdictExitCode(Report.Verdict);   // 0 / 1 / 2
const FString Json   = UMapWardenStatics::ReportToJson(Report);
```

### 5. Scan a `UWorld` directly and write the JSON

```cpp
#include "MapWardenScanner.h"

void GateThisWorld(UWorld* World)
{
    const FMapWardenReport Report = FMapWardenScanner::RunWithProjectSettings(World);
    FMapWardenScanner::LogReport(Report, /*bAllFindings=*/true);

    FString FullPath;
    if (!FMapWardenScanner::WriteReportFile(Report, /*Path=*/TEXT(""), FullPath))
    {
        // A report that could not be written is a failure, not a pass.
    }
}
```

`FMapWardenScanner::FindBestWorld()` gives you the world the console commands would have picked: the running
game, then play-in-editor, then the editor world.

### 6. Change a threshold in code, permanently

Settings are a `UDeveloperSettings`, so the supported route is Project Settings or `DefaultGame.ini`:

```ini
[/Script/MapWarden.MapWardenSettings]
bCheckDefaultNames=True
DuplicateToleranceCm=2.500000
OutOfBoundsRadiusCm=250000.000000
+ExemptTags=LevelArt.Decor
+ExemptFolders=Debug
+ExemptClasses=BP_MyDecalActor
```

For a one-off, per-session change instead, `MapWarden.Exempt 0` and the console commands are the intended
door — nothing in the plugin writes to your config behind your back.

---

## Blueprint API

All of it is on `UMapWardenStatics`, in the `MapWarden` category.

**Doing things**

* `Scan Now` → the report
* `Get Last Report`, `Get Findings`, `Get Verdict`
* `Write Report (Path)`
* `Focus Finding (Index)` — selects the actor in the editor viewport; returns false in a packaged game,
  where there is no viewport
* `Set Exemptions Enabled`, `Are Exemptions Enabled`
* `Set Report Visible`, `Is Report Visible`

**The rules, as pure nodes with no world behind them**

* `Are Duplicates (A, B, ToleranceCm, RotationToleranceDeg, ScaleTolerance)`
* `Is Out Of Bounds (Position, Centre, RadiusCm)`
* `Judge (Findings)` → `Ok` / `Warn` / `Fail`
* `Explain (Finding)` → one sentence about what to do
* `Is Exempt (Actor, Rules)`, `Compute Centre (Actors)`

**Formatting**

* `Format Headline`, `Format Finding`, `Format Coverage`, `Format Count`, `Report To Json`
* `Verdict Name`, `Verdict Exit Code`, `Severity Name`, `Kind Name`

`UMapWardenSubsystem` is a `UWorldSubsystem` and carries `On Findings`, a Blueprint-assignable delegate that
fires after every check — which is how a UMG panel rebuilds itself without polling. `AMapWardenHUD` adds
`Toggle Report`, `Is Report Visible` and `Scan Now`.

A minimal Blueprint panel is three nodes: **Get Game Instance → Scan Now → Format Headline → Set Text**, and
a **Bind Event to On Findings** if you would rather not poll.

---

## The demo map

`Content/MapWarden/Maps/L_MapWardenDemo` (mounted as `/MapWarden/MapWarden/Maps/L_MapWardenDemo`) is a
small, tidy-looking warehouse scene that is deliberately built to **look fine at a glance**. A demo level
that obviously looks broken would prove nothing; the point of the plugin is that these things are invisible
in the viewport, and a duplicated actor standing exactly behind its twin is the clearest possible example.

What is hidden in it, and what the report says about each:

| In the map | The finding |
|---|---|
| `Crate_A` + `Crate_A_Copy`, `Crate_B` + `Crate_B_Copy` | two `duplicate` warnings, 0.00 cm apart |
| `GlassPane`, set to No Collision | `collision` warning — the mesh does have a collision model |
| `Pallet`, Movable, nothing moves it | `mobility` warning |
| `WorkLight_Movable`, Movable light | `mobility` warning |
| `LoadingBayTrigger`, nothing bound | `trigger` warning — clamped, never an error |
| `LostCube`, 252,556 cm out | `bounds` **error** |
| `DecorCrate`, tagged exempt, also No Collision | `info`, and `1 excluded by settings` |

**The dead-reference check is not staged in the demo map**, and that is worth saying plainly: authoring a
genuinely broken `TSoftObjectPtr` in a saved level requires deleting the target asset after the fact, which
would leave the shipped content in a state the editor complains about on load. That check is covered by the
automation tests and documented above instead.

The supporting content, all under `Content/MapWarden/`:

* `Blueprints/BP_MapWardenDemoGameMode` — the demo game mode
* `Blueprints/BP_MapWardenDemoHUD` — derives from `AMapWardenHUD`, so the panel on screen is the real one
  drawn by the plugin, not a mock-up
* `Blueprints/BP_MapWardenDemoProp`, `BP_MapWardenGlassPane`, `BP_MapWardenDecorCrate` — the props
* `UI/WBP_MapWardenDemoPanel` — the buttons: **RUN CHECKS NOW**, **EXEMPTIONS ON**, **EXEMPTIONS OFF**,
  **HIDE REPORT**, **SHOW REPORT**, **WRITE report.json**
* `Materials/M_MapWardenProp` and five instances

Press Play. The auto-scan runs one second after Begin Play, and the panel appears. Press **EXEMPTIONS OFF**
and watch the exempted `DecorCrate` finding climb from `info` back to `warning` and the
`excluded by settings` counter drop to `0` — the exemption list demonstrating that it cannot hide itself.
**WRITE report.json** writes a real `Saved/MapWarden/report.json`.

The demo's exemption entries live in the **project's** `Config/DefaultGame.ini`, not in the plugin, so
installing MapWarden never silently exempts anything in your project.

---

## Settings reference

**Project Settings → Plugins → MapWarden**

| Group | Setting | Default |
|---|---|---|
| Checks | Check Duplicates / Missing Collision / Mobility / Out Of Bounds / Silent Triggers / Dead References | on |
| Checks | Check Default Names | **off** |
| Thresholds | Duplicate Tolerance Cm | 1.0 |
| Thresholds | Duplicate Rotation Tolerance Deg | 1.0 |
| Thresholds | Duplicate Scale Tolerance | 0.01 |
| Thresholds | Out Of Bounds Radius Cm | 100000.0 |
| Thresholds | Default Name Limit | 50 |
| Severity | Duplicate / Missing Collision / Mobility | Warning |
| Severity | Out Of Bounds / Dead Reference | Error |
| Severity | Silent Trigger | Warning (clamped — cannot be an error) |
| Severity | Default Name | Info |
| Exemptions | Use Exemptions | on |
| Exemptions | Exempt Classes / Exempt Folders / Exempt Tags | empty |
| Scanning | Only Placed Actors | on |
| Scanning | Ignored Classes | WorldSettings, LevelScriptActor, AbstractNavData |
| Scanning | Resolve Soft References | off |
| Report | Show Report By Default | on |
| Report | Scan On Begin Play | on |
| Report | Auto Scan Delay Seconds | 1.0 |
| Report | Auto Draw On Any HUD | off |
| Report | Max Report Rows | 14 |
| Report | Report Path | `Saved/MapWarden/report.json` |

**Only Placed Actors** deserves a note. In a running game, a game mode, a player controller, a HUD and a
player state all sit at the origin with an identity transform, and none of them is a level-building mistake.
With this on, only the actors that came with the level are checked. In the editor every actor is a placed
actor and the setting changes nothing.

**Auto Scan Delay Seconds** is not laziness. Actors bind their overlap delegates in `BeginPlay`, and a check
in the same frame as the map load would report every trigger in the level as silent.

---

## Architecture

```
MapWarden (Runtime, PreDefault)
  FMapWardenScanner      the one place that reads a world; turns actors into plain structs
  UMapWardenStatics      the rules, as static functions over those structs, plus the Blueprint API
  UMapWardenSubsystem    UWorldSubsystem: holds the last report, draws it, exposes On Findings
  AMapWardenHUD          draws the report on UCanvas
  UMapWardenSettings     UDeveloperSettings: every switch, threshold and list
  FMapWardenFocus        a delegate the editor module fills in, so the runtime can ask for a selection
  MapWardenCommands      the seven console commands

MapWardenEditor (Editor, PostEngineInit)
  Tools > MapWarden      check the open level, select every finding, write the report
  FocusActor             finds the actor by path, selects it, moves the camera
```

The important line: `FMapWardenScanner` is the only code that touches an `AActor`. It produces
`FMapWardenActorInfo` — names, a transform, a handful of booleans — and everything after that point is pure.
That is why the rules are covered by automation tests that build a level in four lines, why you can call
them from your own tooling, and why the duplicate grid can be checked against a naive comparison.

The dependency arrow points editor → runtime and never back.

---

## Tests

Ten automation tests ship with the source, under `MapWarden.*`, in `CommandletContext` as well as
`EditorContext` so they run on a build machine and not only with an editor open:

| Test | What it pins down |
|---|---|
| `Rules.AreDuplicatesRespectsToleranceRotationAndScale` | the tolerance boundary exactly, and same position with a different rotation, scale or class told apart — including the 359.5° case a naive subtraction gets wrong |
| `Rules.IsOutOfBoundsTreatsTheBoundaryAsInside` | the boundary is inside; a distance, not a box; the centre is the average of what was checked |
| `Rules.ExemptActorProducesNoErrorButIsStillCounted` | by tag, by class, by base class and by folder prefix — made, printed and counted |
| `Rules.JudgeFailsOnlyOnAnError` | twenty warnings are still only a warning; exit codes checked |
| `Rules.SilentTriggerIsAlwaysAWarningNeverAnError` | the clamp holds even when the settings demand an error |
| `Rules.DuplicateGridAgreesWithTheNaiveSearch` | grid vs. naive all-pairs over 5,000 actors, identical pairs, each once |
| `Rules.CollisionFindingNeedsAMeshThatHasCollision` | no finding when there was no collision to throw away |
| `Rules.MobilityAcceptsEveryReasonToBeMovable` | all four legitimate reasons |
| `Report.CoverageIsAlwaysStated` | including the World Partition sentence; an unrun report says so rather than looking clean |
| `Report.FindingsAreExplainedAndOrdered` | every finding carries a sentence and names its actor; stable order; JSON carries verdict, exit code and coverage |

Run them from **Tools → Test Automation**, filter `MapWarden`. On a build machine:

```
UnrealEditor-Cmd.exe <Project>.uproject -ExecCmds="Automation RunTests MapWarden; Quit" -unattended -nopause -nullrhi
```

---

## Troubleshooting

**Every trigger in my level is reported as silent.**
You are checking in the editor, where no `BeginPlay` has run and almost nothing is bound. Check in a running
game, or read the finding as "nothing in the level blueprint references this". It is a warning and cannot
fail your build.

**A Movable light I animate in Sequencer is reported.**
MapWarden can see that a level sequence actor references an actor, but not a binding stored inside the
sequence asset — it takes no dependency on MovieScene. Tag the actor and add the tag to Exempt Tags. The
finding stays visible as `excluded by settings`.

**Nothing is reported at all in my packaged game.**
Check `Only Placed Actors`: an actor spawned at runtime is not checked by default. Also check that your game
mode uses `AMapWardenHUD`, or that **Auto Draw On Any HUD** is on.

**The report says fewer actors than my outliner does.**
That is the "not checked" number on the coverage line: editor-only actors, ignored classes and, in a running
game, everything spawned at runtime. It is printed precisely so the difference is never invisible.

**`MapWarden.Show` says it needs a running game.**
It does — the panel is drawn by an `AHUD` and there is no HUD in an editor viewport. Use `MapWarden.Dump`,
or **Tools → MapWarden → Check This Level**.

**On a World Partition level the report looks too clean.**
It says so on the second line. Only loaded cells were checked. See
[World Partition](#world-partition-what-was-checked-and-what-was-not).

**My build server gets exit code 2 but the JSON says `"verdict": "warn"`.**
The report could not be written, or no level was loaded. Both exit `2` on purpose: a gate that did not run
must never look like a gate that passed. Check the log line from `MapWarden.Gate`.

**`/MapWarden/...` content paths do not resolve.**
The plugin's content root is only mounted once the plugin has been compiled into the project. Build the
project once and restart the editor.

---

## Support

**Support:** [teufelsilvan@gmail.com](mailto:teufelsilvan@gmail.com)
**Documentation:
[wiki.teufel-engineering.com/en/MapWarden/documentation](https://wiki.teufel-engineering.com/en/MapWarden/documentation)**

Copyright 2026 Silvan Teufel. All Rights Reserved.
