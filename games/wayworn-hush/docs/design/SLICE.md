# Wayworn Hush — Vertical Slice Plan

> **Owns:** the smallest buildable thing that proves the aesthetic works, broken
> into incrementally committable steps.
>
> **Status:** proposal.

## The goal of the slice

> One authored region. The character walks around it with free pixel movement.
> One ambient audio bed plays. One inner-monologue trigger fires when the player
> reaches a spot. It looks and *feels* like Emerald/Mother-3 — muted palette,
> pixel-perfect, quiet.

Not in the slice: combat, weather, day/night, camping, skills, inventory,
save/load, menus beyond a title. Those come after the aesthetic is proven. The
slice exists to answer one question: **does walking through a hand-authored 16px
region with an integer-scaled pixel target and an ambient bed feel like the
thing?** If yes, the foundation is right and everything else builds on it.

## Why this order

The steps are ordered so that **each commit is independently sensible** and the
*visible* payoff arrives as early as the plumbing allows. The render target
comes before real art because nothing looks right until the pixel pipeline is
correct — you'd be judging placeholder art through a broken lens otherwise.

---

## Step 0 — Engine: TILE_SIZE per-game *(engine scope)*

Promote `TILE_SIZE` from a shared `constexpr` to a per-game runtime value on
`TileMap` (config over constants). Prison-escape sets 32; the field defaults to
32 so prison-escape is untouched. Wayworn will set 16.

- **Touches:** `engines/engine/include/TileMap.h`, `CollisionSystem.cpp`
  (tilemap cell math reads the runtime value), prison-escape's `main.cpp`/loader
  sets 32 explicitly.
- **Test:** engine Catch2 — a tilemap built at tile_size=16 reports correct
  world extents and cell lookups; prison-escape's 32 still passes existing tests.
- **Verify:** prison-escape builds + boots unchanged (its smoke test still
  passes).
- **Commit:** `engine: TILE_SIZE becomes per-game TileMap field (not constexpr)`

## Step 1 — Scaffold the game target *(builds, runs, blank window)*

Fork the minimum shell from prison-escape: `main.cpp` (crash handler + engine
init + callback registration), a stub `GameLoop.cpp` (empty callbacks), a
`CMakeLists.txt` (renamed targets, `PUBLIC engine` link, `sync-assets-and-config`
target, `Version.h`), added to the top-level `CMakeLists.txt`. Window opens,
clears to the ambient background color, closes cleanly. Wayworn sets
`tile_size = 16`.

- **Also:** add `games/wayworn-hush` to the lizard + cppcheck CI scope lists.
- **Verify:** `wayworn-hush.exe` launches, shows a solid-color window, quits.
- **Commit:** `wayworn-hush: scaffold game target (empty loop, links engine)`

## Step 2 — Integer-scaled pixel render target *(the pixel pipeline)*

Build the 384×216 offscreen FBO. World renders into it at `GL_NEAREST`; blit to
the window at the largest integer scale that fits; letterbox the remainder with
the ambient color. Hook `setOnResize` to recompute the scale. This is net-new
(the engine has no FBO). Decide placement: a game-side render-target helper for
now; promote to engine if selva/prison want it later.

- **Verify:** draw a 16px test-grid texture into the target — grid lines land on
  exact pixel boundaries at every integer scale, no shimmer, no half-pixels;
  window resize keeps it crisp and centered.
- **Commit:** `wayworn-hush: 384x216 integer-scaled pixel render target`

## Step 3 — One authored region renders *(the world appears)*

Decide the **authored-map format** (see decision below), write the loader
(fork prison-escape's `TileMapLoader` parse/stamp; replace procgen with authored
load), author one small region (~48×48 tiles) with a placeholder 16px tileset,
render it through the target via the engine `TileMapRenderer`.

- **Verify:** the authored region renders correctly through the pixel target;
  walkable/solid tiles read from the map data.
- **Commit:** `wayworn-hush: authored-region tilemap format + loader + first map`

## Step 4 — Character walks *(free pixel movement + collision)*

Add the player entity (placeholder 16×24 sprite, 4-dir walk sheet via the engine
`AnimationSystem`), free pixel movement (fork prison-escape's `MovementSystem`
integrator), AABB-vs-solid-tile collision (engine `CollisionSystem`),
camera-follow (engine `CameraSystem` + interp), foot-anchored collider so Y-sort
tucks the character behind objects.

- **Verify:** character walks smoothly in 8 directions, animates per direction,
  stops at solid tiles, camera follows, sprite tucks behind taller objects.
- **Commit:** `wayworn-hush: player entity, free movement, tile collision, camera`

## Step 5 — Ambient audio bed *(the world has a voice)*

Play one looping ambient bed via the engine `AudioSystem::playMusic` (miniaudio,
fade-in, loop). One placeholder OGG. Optionally layer one interval SFX
(bird/wind) via a forked `AmbientSoundSystem`.

- **Verify:** the bed loops seamlessly on region entry, fades in, no clipping.
- **Commit:** `wayworn-hush: ambient audio bed on region entry`

## Step 6 — One inner-monologue trigger *(the protagonist thinks)*

The narrative payoff. A trigger volume in the region; on enter, the protagonist's
thought appears in a minimal text box (Mother-3 register — soft/instant scroll,
no per-syllable clatter per AESTHETIC.md). Build a minimal textbox on
`UIRenderer::drawText`/`measureText` (the engine has no textbox widget; fork
`MenuDialog`'s auto-sizing panel logic for the frame). Fire once per trigger.

- **Backend decision (deferred):** for *one* trigger, a bespoke
  `{trigger_id → text, fired flag}` map is enough — do **not** lift
  `selva::lang`/`insight` yet. Revisit the tiered-register backend when authoring
  the *second wave* of monologues and the register-deepening arc actually needs
  tiers (see AUDIT.md §3). Note in the code why the bespoke version is
  intentional, not a shortcut.
- **Verify:** walking into the spot pops the thought once; it reads correctly at
  the pixel scale; silence-then-thought lands with the right weight.
- **Commit:** `wayworn-hush: inner-monologue trigger + minimal text box`

---

## After the slice (not now)

If the slice feels right: save/load framework, a title screen, then the first
real system (turn-based combat *or* camping *or* weather — design-priority call).
Real art replaces placeholders throughout. The slice's job is done once you can
walk through the region, hear it, and read one honest thought — and it feels like
the game.

## The one real decision inside the slice: authored-map format

Step 3 needs a format for hand-authored regions. No Tiled pipeline exists in the
repo. Options, to decide before step 3:

- **Extend the ASCII `.room` format to full authored maps** — cheapest to wire
  (the parser exists), but ASCII-per-tile doesn't scale to a rich 16px overworld
  with many tile types, layers, and objects. Fine for the slice; a dead end for
  the real game.
- **Author in [Tiled](https://www.mapeditor.org/) (industry-standard, free) and
  import its JSON export** — Tiled is *the* tool for exactly this (layered 16px
  tilemaps, object layers for triggers/spawns/warps). One import-writing step
  now; pays off forever as the map count grows. This is almost certainly the
  correct answer for an authored-overworld game — it's the "one extra tool, done
  right" that monobit doctrine favors over a format that drifts.

**Recommendation: Tiled + a JSON importer.** But this is a genuine
tooling-vs-scope decision worth your explicit call — flagged as a question, not
decided.

## Cross-references

- [AUDIT.md](AUDIT.md) — what each step reuses.
- [SCALE.md](SCALE.md) — the numbers each step implements.
