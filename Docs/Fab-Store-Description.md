# MapWarden — What Is Wrong With This Level

**Documentation: [wiki.teufel-engineering.com/en/MapWarden/documentation](https://wiki.teufel-engineering.com/en/MapWarden/documentation)**

Your content folder can be spotless while your level is a mess — two different questions. A linter asks
whether an asset is named correctly. **MapWarden asks what is actually standing in the map**: how many of it
there are, where it is, and what it is set to.

---

## The failure this exists for

Somebody pressed `Ctrl+D` and did not move the copy. The duplicate is invisible in the viewport, because it
stands exactly behind its twin. It costs memory, it costs a draw call, sometimes it costs you z-fighting that
nobody can reproduce — and it stays in the level for the rest of the project.

The engine's own Map Check is a fixed short list, editor-only, with no thresholds, no exemptions and no
return value a build server can read. Package builders and submission checkers on Fab check the **package**,
not the contents of a level.

---

## Seven checks, each one switchable, each one with its own threshold

A tool with one fixed opinion gets switched off on its first run against a real level, so none of this is
hard-wired.

**1. Duplicates.** Two actors of the same class at practically the same place — under a centimetre apart,
same rotation, same scale. All four conditions have to hold: two crates a millimetre apart and turned ninety
degrees are a wall somebody built on purpose.

**2. Collision missing where it was expected.** A static mesh actor set to `No Collision` whose mesh *does*
bring a collision model — somebody built that collision, and this placement throws it away. **The reverse is
deliberately not a finding:** a mesh without any collision model is the mesh author's decision, not the
placer's.

**3. Mobility.** Lights and meshes left on `Movable` that demonstrably never move: no movement component, no
simulating body, not attached to anything movable, not referenced by a level sequence. Shadow work paid for
and never used.

**4. Out of bounds.** The actor further from the centre of the level than the radius you set — the cube
somebody lost while dragging. The centre is the average position of what was checked, not the world origin.

**5. Silent triggers.** Overlap volumes with nothing bound to their delegates and no reference from the level
blueprint. **This one is a warning and can never be an error**, whatever you set it to, because a binding can
be made at runtime from code this scan never loaded — and the report line says exactly that.

**6. Dead references.** Soft references on placed actors that no longer resolve to a package that exists.

**7. Default names.** More than N actors still called what their class called them. **Off by default** —
how many `StaticMeshActor_231` you can live with is taste, and taste does not belong in a gate unless you
put it there.

---

## The duplicate search is the fast one

Comparing every actor against every other one is quadratic — twenty thousand actors means two hundred million
distance tests. MapWarden puts the actors on a uniform grid whose cell size *is* the duplicate tolerance, so
each actor is only compared against its own cell and the twenty-six around it. One of the shipped automation
tests runs the grid and a naive all-pairs comparison over five thousand actors and requires **identical**
pairs — a fast search that quietly misses one is worse than a slow one.

---

## The honest part: two things it cannot do

Both are on the report, not buried in a readme.

**It cannot know what you meant.** A crate with no collision may be decoration behind glass. So there are
exemption lists **by class, by outliner folder and by actor tag** — and what was exempted is still counted
and still shown as `excluded by settings`, with the finding still named. An exemption list that could hide
its own effect would turn a level green from a settings page, which is what a gate exists to prevent.
`MapWarden.Exempt 0` shows the same level both ways in one keystroke.

**It checks the map that is loaded.** With World Partition, only the cells in memory exist. So the report
names how many actors were checked and from which levels, and on a partitioned world it says in warning
colour that **unloaded cells were not checked**. The JSON carries the same flag.

*"Clean" must never be allowed to mean "nobody looked."*

---

## The report

Drawn on `UCanvas` from an `AHUD`, so it survives a cooked Shipping build.

```
MapWarden WARN | actors <count> checked in <level> | errors <n>  warnings <n>  info <n> | <n> excluded by settings | scan <n> ms
```

Then the findings, errors first, each naming the actor, what was measured and the threshold it broke, plus
one plain sentence about **what to do**:

*SM_Crate_2 and SM_Crate_1 are the same class in the same place with the same rotation and scale. One of
them is invisible behind the other, and it still costs memory, a draw call and sometimes z-fighting.
Delete one, or move it if the stack was deliberate.*

When there is nothing to report it turns green **and lists what it checked**, switched-off checks in warning
colour, so green is never mistaken for "never ran".

---

## In the editor

**Tools → MapWarden** runs the same checks on the level that is open, with no game standing: *Check This
Level* (headline as a toast, whole report in the Output Log), *Select Everything With A Finding* (every actor
a finding is about, selected at once) and *Write Level Report*. `Focus Finding` selects an actor and moves
the viewport camera to it, from Blueprint or from your own tooling.

---

## One command on the build server

`MapWarden.Gate` writes `Saved/MapWarden/report.json` and ends the process with **0** when clean, **1** when
there are only warnings and **2** when there is an error — the same three codes as **AssetWarden**,
**LocaleGuard**, **BindGuard**, **ReadGuard**, **WidgetLedger**, **LoadLens** and **HeapCensus**. **Eight
tools, one convention.**

A report that could not be written, or a gate that found no loaded level, exits `2`. A gate that did not run
must never look like a gate that passed.

---

## MapWarden or AssetWarden?

**AssetWarden** checks the **content folder** — naming, location, references — including everything nobody
has loaded. **MapWarden** checks the **map**: the same mesh can be flawless on disk and placed three times in
exactly the same spot. Placement, transforms, mobility and overlap bindings only exist in a level. They do
not overlap; run both.

---

## Built to be trusted

The rules are **static functions over plain structs** — `AreDuplicates`, `IsOutOfBounds`, `Judge`, `Explain`
— with no world, no subsystem and no actor behind them, which is why **ten automation tests** cover them and
why you can call them from your own tooling. Among the tests: the grid against the naive comparison, and the
one asserting a silent trigger can never be raised to an error.

Two modules, and the dependency arrow points one way only. **The runtime never depends on the editor**, which
is why the gate runs in a commandlet and the report works in a packaged build.

---

## What it is not

It does not move, delete or edit anything in your level, and it has no opinion about your naming conventions.
It reports what is duplicated, mis-set, lost, unlistened-to and broken — with names and numbers — and it
tells you what it did not look at.

---

## What the screenshots show

Every number below is read off the demo level running on one machine. It is a measurement of that demo,
not a promise about your project.

* **Level report** — headline `MapWarden FAIL | actors 28 checked in L_MapWardenDemo | errors 1
  warnings 6 info 1 | 1 excluded by settings | scan 0 ms`, and under it the coverage line: *28 actors
  checked in 1 level(s); 15 not checked*. The one error: `LostCube is 252,556 cm from the centre of the
  level (limit 100,000 cm)`.
* **Exemptions off** — the same level, one keystroke later: `errors 1 warnings 7 info 0 | 0 excluded by
  settings`. `DecorCrate` moves from the grey `[excluded by settings]` line up into the warnings. Nothing
  was hidden; it was only labelled.
* **Gate report written** — `Wrote Saved/MapWarden/report.json — the same file MapWarden.Gate writes on a
  build server.`
* **Editor: exempt by tag**, **Project settings**, **Exemptions and report** — the same checks from the
  editor menu, and the thresholds where you set them.

Two duplicate pairs sit `0.00 cm` apart at the same rotation and scale — the `Ctrl+D` nobody moved.

---

## Technical Details

**Documentation: [wiki.teufel-engineering.com/en/MapWarden/documentation](https://wiki.teufel-engineering.com/en/MapWarden/documentation)**

**Features**

* Seven checks over placed actors: duplicates, collision, mobility, bounds, silent triggers, dead
  references, default names
* Each check switchable on its own, with its own threshold and its own severity
* Duplicate search on a uniform grid — linear rather than quadratic, verified against a naive comparison
* Silent-trigger findings clamped to Warning and never an error, by design
* Three exemption lists (class, outliner folder, actor tag) with a visible `excluded by settings` count that
  cannot hide itself
* Coverage stated on every report; World Partition says in words that unloaded cells were not checked
* On-screen `UCanvas` report that survives a cooked Shipping build
* Green report lists what was checked; switched-off checks printed in warning colour
* `MapWarden.Gate` → `report.json` and exit 0 / 1 / 2, the same convention as seven sibling tools
* Seven console commands, all of which work with or without a running game
* Editor menu under **Tools → MapWarden**, including "select every actor with a finding"
* Full Blueprint API, including the rules as pure nodes
* 10 automation tests over the pure rules, in `CommandletContext` as well as `EditorContext`

**Code Modules**

* `MapWarden` — Runtime, `PreDefault`
* `MapWardenEditor` — Editor, `PostEngineInit`

**Number of Blueprints:** demo only (map, game mode, controller, pawn, panel)
**Number of C++ Classes:** 5 (subsystem, HUD, statics, settings, scanner) plus the focus delegate and the
editor module
**Network Replicated:** No
**Supported Development Platforms:** Windows (Win64)
**Supported Target Build Platforms:** Windows (Win64)
**Engine Version:** 5.8
**Dependencies:** None beyond the engine
**Third-party code:** None

**Support:** [teufelsilvan@gmail.com](mailto:teufelsilvan@gmail.com)
