# Selva Oscura — Development Pillars

The non-negotiable principles that shape how the game is built. These sit
above gameplay design, above engineering style, above tooling: they
govern HOW work happens, not WHAT we work on.

If a proposed change conflicts with a pillar, the change loses. If two
pillars conflict, the earlier-listed one wins.

---

## 1. Simplest possible foundation, built up incrementally

**Every new feature starts at its simplest viable form.** A movement is
a single clip on a single button — no physics, no chaining, no edge
cases. A combat action is one attack hooked to one input. A camera
behavior is fixed-distance follow. The first version of any system
does ONE thing and is provably correct at doing that one thing.

**Then we layer.** Once the foundation is in place, in-game, and feels
right at the most minimal level, we add the next thinnest layer. Edge
cases get added one at a time, each in its own pass, each tested
before the next is layered on. A jump goes: clip fires on input → adds
ground detection → adds Y-arc → adds jump-attack chain → adds context-
sensitive variants. Never all five at once.

**Why this matters:** every system in Selva Oscura that had trouble in
development was one that grew faster than it was understood. The
animation transition stack went through Path-A → Path-B → Path-A
because layers got added without confirming each was needed. The
leg-spasm bug class persisted for months because multiple bridging
mechanisms were stacked without isolating which one was load-bearing.
Built layer-by-layer, with each layer's purpose proven before the
next, those classes of confusion don't form.

**Signs you're violating this pillar:**
- "Let me also handle X while I'm in here." (No — file X for later.)
- "This needs a system to manage Y first." (Probably not. Try without.)
- "We'll need this anyway when Z lands." (Maybe. Add it when Z lands.)
- Proposing a fix that introduces three new files. (One file. Maybe.)
- A first version that already supports configurations you haven't
  decided on yet.

**Operational rule:** when starting a feature, write down what the
single thinnest viable version is. Build only that. Ship it. Play it.
Decide what's missing. Build only the next thinnest layer. Repeat.

---

## 2. Root cause over symptom, every time

When a bug surfaces, fix the assumption that broke, not the visible
behavior. A foot teleport gets fixed at the data-flow level, not by
clamping the foot's position. A leg spasm gets fixed by structural
separation of bridging mechanisms, not by tuning timing constants.

**Bandaids are anti-patterns, not options.** If the first proposal
addresses the symptom, the proposal is wrong — re-derive it from the
underlying assumption.

When a fix lands, the bug class is gone, not just this instance.

---

## 3. Prove with data, never theorize past the evidence

When a bug's cause is unclear, add diagnostic logging first. Read what
the system actually produces, not what it should produce. Most
animation bugs in this project have been caught by reading per-frame
world positions — values sitting in the log that I read past for
sessions because I was theorizing instead of looking.

When data contradicts theory, the theory is wrong. Update the theory.

---

## 4. Subtract before adding

Every new feature carries debt. Before adding a system, check whether
an existing one already covers it (sometimes badly — that's OK, the
fix is to improve it). Before adding a tunable, check whether existing
ones can be repurposed. Before adding a track, channel, layer, or
slot: ask whether one of the existing ones isn't pulling its weight.

The animation transition stack is +28 lines net across the harmony-
rule refactor, but ~600 lines of code were both added AND removed in
the process — the architecture is structurally cleaner because every
mechanism that exists has a single clear job.

---

## 5. Tripwires for invariants

When a structural rule is established (the harmony rule, root motion
separation, etc.), add a runtime log that fires if the rule is
violated. The rule isn't just doctrine — it's enforced by
self-detection. Future regressions can't hide.

Example: `[!!! OVERLAP]` in PoseSampler logs if inertialization ever
fires during an active loco crossfade. If that line ever appears in
combat-debug.log, a code change has re-introduced the failure mode.

---

## 6. Author the simplest content first

This applies to art, music, dialog, encounter design. The first
version of any content is the rough, single-pass version. We play
through with placeholder assets, identify what's missing, and iterate.
We don't author final-quality assets until the rough version has
proven the design.

This is asset-side mirror of pillar 1.

---

## 7. Pacing: small victories

Long-horizon work is broken into small, shippable victories. Each
session ends with something playable and demonstrably better than the
previous session. We don't accumulate three weeks of work-in-progress.

See memory `feedback_3d_pacing.md` for the operational rule.

---

## 8. Structures own their seam with terrain

When a static-mesh structure (chapel, ruin, well, archway, cave mouth)
meets the terrain, **the structure brings the geometry that hides the
seam — terrain stays simple.**

**When this pillar applies:** any static mesh where the player will
walk close enough to see where the asset meets the ground. Practical
trigger:

- Structure sits ON terrain and is visible from outside (chapel,
  ruin, watchtower, well, statue base, large gravestone, archway).
- Structure passes THROUGH terrain into an underground continuation
  (descent corridor, cave mouth, stair shaft, dungeon mouth).
- Structure's outline is sharper than 2× terrain mesh resolution
  (Selva terrain is 2.67 m/vertex; anything with edges crisper than
  ~5 m).

**When this pillar does NOT apply:**

- Foliage-scattered props (rocks, debris, bones, broken statuary).
  These sink into terrain naturally and look fine.
- Anything fully indoors / on another structure's floor slab (chests,
  altars, interior furniture).
- Anything airborne (banners, hanging cages, lanterns).

Rule of thumb at design time: "would the player walk close enough to
see where this meets the ground?" Yes → pillar 8 applies. No → ignore.

Concretely, every above-ground structure ships with:

1. **An authored floor slab** at the elevation the player walks on,
   as part of the mesh. The structure does NOT use terrain as its
   floor. Player walks on mesh, not heightmap.
2. **Walls/ceilings closed enough** that no terrain is visible from
   inside the structure regardless of camera angle.

Terrain is then controlled through a SINGLE registration call:

```cpp
engine::world::registerStructureFootprint({
    .center_xz = ...,
    .half_extents_xz = ...,
    .debug_name = "chapel_footprint",
});
```

This one call wires BOTH sides of terrain handling:
- **Physics:** registers a `Hole`-mode `TerrainModifier`, so the
  terrain trimesh has its quads dropped inside the footprint rect.
  Player never collides with terrain inside the chapel.
- **Render:** the terrain fragment shader queries the registered
  footprint list and discards pixels inside any footprint rect.
  No visible terrain inside the chapel.

Single source of truth, no physics-vs-render drift.

**The quad-margin rule.** Heightmap terrain interpolates linearly
between vertex Y values, and quads are dropped per-CENTROID. This
creates two boundary artifacts:

1. *Drop boundary:* a quad whose centroid is just OUTSIDE the
   footprint survives, but uses vertices that are INSIDE (depressed
   by any neighboring FlushAt modifier). Result: a tilted "surviving
   boundary" triangle floats in airspace where the chapel mesh would
   otherwise have been. **Player gets stuck on it.**
2. *Visual seam:* the quad straddling the footprint edge has one
   vertex inside (dropped) and one outside (rendered), producing
   visible terrain that ends mid-quad.

Both are fixed by extending the footprint rect by **≥ one terrain
quad spacing** past the structure mesh's outer face on every edge.
That guarantees every quad whose centroid falls outside the rect
ALSO has all four vertices outside, eliminating tilted survivors.

For Selva (`subdivide=384`, `world_extent=512`, spacing=1.333m), the
footprint extends 1.33m past the chapel's visible walls on each edge.

**Asymmetric blend modifiers.** Where a separate `FlushAt` modifier
controls terrain Y outside the structure (e.g. "terrain meets plinth
bottom around the chapel"), use per-side `blend_pad_neg_x/pos_x/
neg_z/pos_z` overrides. The default `blend_pad` applies on sides where
soft terrain transition is desired; set the per-side override to 0 on
sides where a sharp edge is needed (e.g. `blend_pad_pos_z = 0` to make
terrain return to natural Y immediately at the door plane, so the
door threshold doesn't sit on a 2m ramp).

**For sloped tunnel mouths:** when the chapel's descent corridor exits
the back wall, its ceiling top is briefly above natural terrain Y for
a few meters before going underground. Use a `FlushSlope` modifier
covering that wedge — terrain Y interpolates from the corridor
ceiling-top-at-back-wall down to natural Y at the bury point. Past
the wedge's back edge, the corridor is deep enough that natural
terrain covers it without help.

(Why `subdivide=384` rather than higher: terrain MeshShape::Create
scales linearly with triangle count. At 192² → 440ms × 2 preloads.
At 1024² → ~12s × 2 preloads = 24s+ boot hang. 384² = ~1.7s × 2 =
3.4s, acceptable. Tune resolution to balance quad-margin cost vs
boot time.)

**Why this pillar:** the chapel went through 6+ rounds of terrain-
modifier tuning (per-side overhangs, FlushSlope dance, shader-discard
rects, blend-pad sizing, plateau-vs-hole interaction). Every iteration
re-derived a relationship that the mesh itself could have just owned.
The bug class is "two systems (mesh shape + terrain rules) must agree
on a per-feature contract that drifts the moment either side changes."
Skirt geometry collapses both sides into one: the mesh declares its
footprint, terrain depresses inside it, the skirt hides whatever's
left.

This is also the standard AAA pattern. Heightmap terrain can't
represent overhangs or multi-level structures; the universal answer is
"structure is a separate mesh placed on top, mesh has skirting that
hides the seam." Elden Ring, every Unreal open-world, every Unity
Terrain project does this. We had been doing the inverse — terrain
custom-shaped per structure — and burning hours per chapel iteration.

**Signs you're violating this pillar:**
- Wiring `Hole` modifiers directly instead of going through
  `registerStructureFootprint` (manual physics-only carve, render side
  drifts).
- Setting shader-discard rect uniforms directly from C++ instead of
  reading from `structureFootprintAt(N)` (manual render-only carve,
  physics side drifts).
- Footprint rect sized to match the chapel walls exactly (no
  quad-margin). Player will get stuck on a tilted boundary triangle.
- Hardcoded back-edge / front-edge / corridor-strip-length constants
  in PhysicsScene — these are derived from JOINT + heightmap math,
  not free parameters.
- Mention of "match this specific primitive's AABB" in modifier
  values.

**Operational rule:** when authoring a new structure that touches
terrain, the C++ side is exactly two calls:

```cpp
// Optional: exterior plateau if terrain should meet a specific Y
// around the structure (e.g. terrain meets plinth-bottom outside chapel).
engine::world::registerTerrainModifier({...FlushAt...});

// REQUIRED: physics + render terrain removal inside the structure.
engine::world::registerStructureFootprint({
    .center_xz = ...,
    .half_extents_xz = {visible_half_w + quad_margin,
                        visible_half_l + quad_margin},
    .debug_name = "your_structure",
});
```

---

## 9. Everything preloads before the main menu

All assets the player can possibly hit during a game session
(meshes, trimesh shapes, audio, animation clips, scene bodies for
every reachable scene) load before the main menu is interactive.
Mid-session loading is forbidden.

**Why:** Selva is a soulslike. Gameplay rhythm — parry windows,
i-frame timing, traversal momentum — depends on consistent
frametimes. A 500 ms hitch during a door transition or first-time
SFX play breaks the contract the genre makes with the player.
Boot can take 5–10 seconds; a single mid-session hitch is worse
than 5 extra seconds at startup.

**Concretely:**

- Every scene listed in `assets/scenes/scenes.json` calls
  `preloadAssets()` at boot, regardless of whether it's the default
  spawn scene. `SceneBootstrap::loadAllScenes()` is the chokepoint;
  don't add a "lazy" flag.
- Every audio bank registered in `audio.json` decodes at audio init,
  not on first `playSfx`.
- Every animation clip referenced anywhere loads in
  `initSkeletalAssets`.
- Static meshes, terrain trimesh, all Jolt shapes — preloaded.
- The single exception is **save data**, which loads only when the
  user clicks "Continue" / "Load" from the main menu (it's data,
  not gameplay-rhythm-critical).

**Signs you're violating this pillar:**

- A `lazy_load` flag anywhere.
- A `preload = false` JSON field.
- A "Loading…" overlay that appears during gameplay (not boot, not
  the main-menu→Playing transition — actually during play).
- A code path that checks "is this asset loaded yet?" outside of
  init.
- "We can defer this to the first time the player needs it."

If boot becomes slow enough to be a problem, the answer is to make
boot loading faster (parallelism, format optimization, smaller
assets) — not to defer work into gameplay.

**Boot UX:** the engine renders a "Loading" screen with a growing
list of completed init steps + a current step, so the user sees
progress. See `Engine::renderLoadingFrame`. Call it at every
boot-phase boundary in `main()`.

---

## Anti-pillars

Things that are NOT pillars, and that we explicitly reject:

- **Engineering elegance for its own sake.** If the codebase looks
  unprincipled in places but ships a fun game, that's the right
  tradeoff. We refactor when fragility blocks work, not preemptively.
- **Feature parity with reference games.** Selva Oscura is not
  Souls/Sekiro/ICO. It draws from them; it doesn't copy. Features
  ship because they serve this game, not because reference games have
  them.
- **Tooling perfection.** The build system, lint pipeline, and test
  harness are good enough. Time spent there beyond "good enough" is
  time not spent on the game.

---

## Version-bump doctrine

Selva Oscura uses semantic versioning, but interpreted for a game
project rather than a library. Strict SemVer was designed for software
where the public API is a real concept (function signatures, ABI
compatibility); a game has a different surface, so the bump meanings
need to be defined explicitly.

The branch prefix (`<bump>/selva-oscura/<issue>-<desc>`) drives the
release workflow's version bump. **One PR = one bump.** The size of
the PR doesn't change which digit moves. The *kind* of change does.

### MAJOR (`X.0.0`)

Reserved. A major bump happens only when one of these is true:

- **First public ship**: `0.x.x` → `1.0.0` is the *one* uncontroversial
  major. It marks the transition from "in development" to "this is the
  game players bought."
- **Save-data break** that requires migration or fresh start. Players
  who have a save file from `X.y.z` cannot use it in `(X+1).0.0`.
- **Identity shift in a released product.** If a player who bought
  Selva Oscura at v1.0.0 would feel "wait, this is a different game
  now," that's a major. (This bar is high and we expect to never hit
  it.)

**MAJOR is NOT used for:**
- Large PRs. A foundational branch bundling many systems is still a
  minor if nothing in it breaks. (See "On large PRs" below.)
- Breaking changes to internal C++ APIs. We have no external
  consumers; internal refactors are not breaking changes.
- Significant engineering complexity. Effort and major-version-worthy
  aren't the same axis.

### MINOR (`x.Y.0`)

A **new system** ships into the engine or game. This is the
"ordinary milestone" bump — what 90%+ of pre-1.0 PRs should be.

Examples that warrant a minor:
- A new combat subsystem (attack chains, parry, weapon class).
- A new world subsystem (terrain heightmap, atmospheric scattering).
- A new AI subsystem (behavior trees, perception).
- A new gameplay loop (gathering, crafting, encounter design).

Multiple new systems in one PR still bump exactly one minor. The PR
description and the changelog body document the size; the version
number documents the *direction* (forward, non-breaking).

### PATCH (`x.y.Z`)

A fix or polish pass on existing behavior. No new system, no new
player-facing feature. Save-data unchanged.

Examples:
- Bug fix in an existing system.
- Balance tweak (damage values, animation timing constants).
- Performance improvement.
- UI polish on existing screens.

### CHORE (no bump)

Internal-only changes: refactors, comment cleanup, lint fixes, CI
tweaks, dependency upgrades that don't change observable behavior,
docs-only changes.

The changelog section for these is the single word `skip`.

### DOCS (no bump)

Documentation-only PRs that don't touch code or runtime behavior.
Same as CHORE for changelog purposes (`skip`).

### On large PRs

Sometimes — especially in foundational development phases — a single
PR carries multiple new systems. **Don't escalate to MAJOR to
compensate for size.** The version-bump semantic is "what kind of
change is this," not "how much work was this." A foundational PR
with five new systems and zero breaking changes is a minor, and the
changelog body explains the size.

When this happens, the changelog body should explicitly note the
unusual scope (e.g., "This release bundles foundational development
across multiple subsystems") so a reader who only sees the version
delta isn't misled.

### Dev semver vs release semver — future consideration

Currently, `CMakeLists.txt`'s `project(SelvaOscura VERSION ...)` is
the single source of version truth: it drives both the binary's
internal version constant and the release tag. There is no
separate "release version" decoupled from the engineering version.

If Selva Oscura ever reaches a state where:
- Players are getting builds from the public releases repo, AND
- The build cadence is faster than the player-facing version cadence
  should be (e.g., we ship five engineering minors per month but
  only want to show one player-facing release a month),

then we'll introduce a `DISPLAY_VERSION` string decoupled from
`CMakeLists.txt`'s VERSION. Until that need is concrete, the two
stay unified. (Per pillar 1: don't design for hypothetical future
requirements.)
