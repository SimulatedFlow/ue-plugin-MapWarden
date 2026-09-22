# MapWarden — What Is Wrong With This Level

Audits what is actually **placed in a level**, rather than what is in your content folder: actors
duplicated on top of each other, meshes with collision switched off, lights left on Movable that nothing
moves, actors far outside the level, trigger volumes nobody listens to, and references that no longer
resolve — with a gate that fails a build.

**Documentation:
[wiki.teufel-engineering.com/en/MapWarden/documentation](https://wiki.teufel-engineering.com/en/MapWarden/documentation)**

Unreal Engine 5.8 · Win64 · full C++ source · no third-party code.

---

## The seven checks

Each one has its own switch and its own threshold, in Project Settings → Plugins → MapWarden.

| # | Check | Default severity | Default |
|---|-------|------------------|---------|
| 1 | **Duplicates** — same class, under 1 cm apart, same rotation and scale | Warning | on |
| 2 | **Collision** — `No Collision` on a mesh whose asset *does* have a collision model | Warning | on |
| 3 | **Mobility** — Movable lights and meshes that nothing moves | Warning | on |
| 4 | **Out of bounds** — further than 100 000 cm from the centre of the level | Error | on |
| 5 | **Silent triggers** — overlap volumes with nothing bound to them | Warning (clamped) | on |
| 6 | **Dead references** — soft references that no longer resolve | Error | on |
| 7 | **Default names** — too many `StaticMeshActor_231` | Info | **off** |

Check 5 can never be an error, whatever you set: a binding can be made at runtime and MapWarden cannot see
that. Check 7 is off because how many default names a level may contain is taste, and taste does not belong
in a gate unless you put it there.

---

## The gate

```
MapWarden.Gate
```

Checks the loaded level, writes `Saved/MapWarden/report.json` and exits **0** clean, **1** warnings,
**2** errors — the same three return codes as AssetWarden, LocaleGuard, BindGuard, ReadGuard, WidgetLedger,
LoadLens and HeapCensus.

Console commands: `MapWarden.Scan`, `.Show`, `.Hide`, `.Dump`, `.Report`, `.Gate`, `.Exempt 0|1`.
Editor menu: **Tools → MapWarden**.

---

## Two things it cannot do, on the report rather than in a footnote

**It cannot know what you meant.** A crate with no collision may be decoration behind glass. So there are
exemption lists by class, by outliner folder and by actor tag — and what was exempted is still counted and
still shown as `excluded by settings`, so the list can never quietly make a level look clean.

**It checks the map that is loaded.** With World Partition only the cells in memory exist. The report names
how many actors were checked and which levels they came from, and on a partitioned world it says in words
that unloaded cells were **not** checked. Clean must never mean nobody looked.

---

## MapWarden or AssetWarden?

* **AssetWarden** checks the **content folder**: is the asset named correctly, is it in the right place,
  does it point at something that no longer exists.
* **MapWarden** checks the **map**: what is standing in it, how many of it there are, where, and with what
  settings.

The same mesh can be flawless on disk and placed three times in the same spot. They answer different
questions and they are meant to be run together.

---

Copyright 2026 Silvan Teufel. All Rights Reserved.
Support: [teufelsilvan@gmail.com](mailto:teufelsilvan@gmail.com)
