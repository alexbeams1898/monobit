# Wayworn Hush — HUD Layout

> **Owns:** where the game's content surfaces on screen — the **fixed regions** the
> reading box, thought box, action menu, and notifications render into, and the
> spatial hierarchy that lets the player read the *kind* of content by *where* it
> appears. Replaces the current content-sized panels (whose geometry shifts per
> message) with authored, non-overlapping rectangles.
>
> **Status:** design proposal. The cognition engine + a single content-sized box
> ([ThoughtBox](../../src/ThoughtBox.cpp)) + notifications ([Notify](../../src/Notify.cpp))
> are built; this doc specs the fixed-region layout that replaces the box's
> content-sizing. Design before implementing — the HUD is central and load-bearing.

## tldr

Three **fixed screen regions** — HUD **overlays pinned to fixed screen
positions** (like subtitles, or Disco Elysium's skill-voices/dialogue). The
**world + player render across the whole screen behind them** and scroll freely;
the regions do **not** follow the player. Each is a stable rectangle content
renders *into*, so the layout never shifts, nothing overlaps, and the player
learns "what kind of thing is this" from *where on screen* it appears:

```
┌──────────────────────────────────────────────┐   the WORLD fills the whole
│  the world + player render behind everything │   frame behind the HUD; the
│  and scroll freely (player can be anywhere)  │   camera follows the player,
│                                              │   the HUD bands do NOT.
│      · · · · · · THOUGHT band · · · · · · ·   │  upper — SUBJECTIVE
│           (faculty-hued realization)         │  (interiority, overlaid on top)
│                                              │
│                                              │
│    · · · · OBSERVATION / ACTION band · · · ·  │  lower — OBJECTIVE + DOING
│         reading (plain)  ·  [ menu ]         │  (world + your hands)
│                              [ notifications ]│  right, level w/ lower band
└──────────────────────────────────────────────┘
```

**Mind above, world + hands below** — a *screen* hierarchy, not a world one.
Thoughts (emergent, subjective) surface in the upper screen band; objective
readings and the action menu (the *doing* layer) in the lower band; notifications
right, level with the lower band. Wherever the pilgrim is standing in the world,
a thought appears in the upper band and a reading in the lower band. Content
longer than a region **paginates** (Space turns the page, then dismisses) — the
region never resizes.

## Why fixed regions

The current box is **content-sized** — its height/position derive from how many
lines fit, so its geometry shifts per message. That blocks a stable HUD: you
can't lay anything out around a moving box, overlap is a constant hazard, and
every surface re-solves "does this fit / where does the next one go." Fixed
regions invert it: **author the rectangle once; render content into it.** One box
shape, no max-height math, no overlap — and a predictable frame to build the rest
of the HUD around.

## The three regions

Each region is an **anchor + offset + size** in reference-canvas units (see
Dimensions below), scaled to the window — resolution-independent, not raw pixels.
Authored in `config/hud.json` so the layout is tunable, not magic numbers.

| Region | Anchor / band | Holds | Chrome |
|---|---|---|---|
| **THOUGHT** | top-center, upper band | subjective thoughts (realizations, conclusions) | faculty-**hued** border + tint |
| **OBSERVATION / ACTION** | bottom-center, lower band | objective readings + the action menu | neutral chrome |
| **NOTIFICATION** | center-right, at lower-band top | ambient toasts (EXP, "1 new observation") | minimal, fades in place |

- **THOUGHT (upper).** A thought is *what you made of it* — it surfaces above,
  faculty-hued (perception/reason/wonder color), reading as interiority. Fixed
  rect; the reading paginates within it.
- **OBSERVATION / ACTION (lower).** The objective reading ("A stone, half-sunk")
  and the action menu ("Clear the moss / Leave") both live here — they're the
  *engaging-the-object* layer, grounded near the player. Reading and menu share
  the region (the menu replaces/overlays the reading when it opens; they don't
  co-display). Neutral chrome (not faculty-hued — objective).
- **NOTIFICATION (right).** Ambient toasts, right-aligned, top edge level with the
  OBSERVATION/ACTION region, stacking upward. Fade in/out in place, no movement.

Regions are **non-overlapping by construction** — a thought (upper) and a reading
(lower) can be on screen at once without collision.

## Content → region routing

The engine tags each surfaced line by `PendingLine::kind` (Observation vs
Thought). Both kinds render in the one LOWER band, distinguished by **register,
not position** — the *look* signals the kind, so no literal "Observation/Thought"
label is needed (keeps the HUD minimal, per AESTHETIC.md).

- `LineKind::Observation` → **perception window**: a dark EarthBound/Mother-3
  dialogue box, light text, no header. He is *perceiving* the world in the moment
  — objective, not written down.
- `LineKind::Thought` → **notebook entry**: an aged-paper page, ink text, a
  faculty-hued left margin rule, and a header (dateline · faculty · rarity · New).
  He is *reflecting / writing it down* — subjective.
- An action **menu** → perception window (a deed on the object you perceive, not
  an entry).
- A `notify::push` → NOTIFICATION region (centered in the gap right of the box).

The upper THOUGHT region stays defined in config for a possible future re-split,
but both kinds currently share the lower band.

### The dateline — WorldClock seam

The notebook header's dateline comes from
[`worldclock::stamp()`](../../src/WorldClock.cpp) — a real in-world clock that
ticks on world time (held while paused). Today it's a placeholder `Day N`; the
day/night + calendar arc grows the module's internals without touching the box.
Diegetically the dateline is meant to gate on carrying a **watch** (a planned key
item): the world always knows the time; whether the notebook *shows* it is a
caller decision. Observations carry no dateline (they aren't written down).

### Over-head thought bubble (the head signal)

A small **white thought bubble** pops over the player's head while a thought
reading is on screen — a cloud on a short stem whose base sits at the head. It's a
world-anchored sprite (like the observable [Glimmer](../../src/Glimmer.cpp))
parented to the player — [HeadMarker](../../src/HeadMarker.cpp), driven by
`thought_box::activeThought`. Deliberately **minimal**: white (not faculty-tinted
— the hue lives in the notebook entry), just marking *he's having a thought*.
Observations get no bubble. Placement/feel in `config/head_marker.json`; the cloud
sprite is a placeholder until the art pass.

### New-entry sound

A new thought (a fresh notebook entry being written) plays a pen-on-paper cue
(`notebook_sound`, `config/observation_box.json`) in place of the generic appear
tone — the sound of writing it down.

## Pagination (overflow within a fixed region)

A region is a fixed rectangle with a known line capacity (region height ÷ line
height, minus padding + any header). If a reading's wrapped lines exceed that
capacity:

- Split into **pages** of ≤ capacity lines.
- The typewriter reveals the current page; when it finishes, **Space turns to the
  next page** (a subtle "▾ more" affordance). On the last page, Space **dismisses**
  (the existing Souls-like manual read — pagination just adds page-turns before
  dismiss).
- Notifications are short by nature and never paginate.

This keeps the region's shape constant no matter the content length — the fixed
height is honored, long content flows through it a page at a time.

## Dimensions — reference canvas + anchors (resolution-independent)

The HUD renders in window space, and the game should run **fullscreen** (the
common default) at whatever resolution the display is. So HUD geometry must **not**
be raw pixels for one window size (today's bug: everything is hardcoded for a
1536-wide window and would break fullscreen / at other resolutions).

**The model — a reference canvas + per-region anchors:**

- The HUD is authored against a fixed **reference canvas** (a 16:9 design space,
  e.g. `1920×1080`). All region rects are authored in canvas units.
- Each region is an **anchor + offset + size**: it pins to a screen edge/corner
  (e.g. THOUGHT = top-center, NOTIFICATION = center-right) with an offset and a
  size, rather than an absolute x/y. Anchoring means a region stays "in its
  corner" at any window size instead of drifting.
- At render, the canvas maps to the actual window by a **uniform scale** (fit the
  16:9 canvas into the window), and regions are placed by their anchors. Uniform
  scale (not stretch) keeps text proportions correct.
- **Aspect ratios:** the world already renders 16:9 and letterboxes. The HUD
  anchors to a **16:9 safe area** centered in the window — so on ultrawide (21:9)
  or 16:10 the HUD stays within the same 16:9 band as the world (no regions
  drifting to the far periphery, no stretch). Bars, if any, are outside the safe
  area.

So one authored layout (canvas units + anchors) is correct at every resolution
and in fullscreen. `config/hud.json` holds the reference canvas size + each
region's anchor/offset/size. This is the **foundation the fixed regions sit on**
— it comes first, so region rects are authored once, not re-done per resolution.

### Fonts scale with the canvas too

Region rects being canvas-relative is only half of it: the **text inside** must
scale the same way, or a region shrinks on a small window while its 48px text
overflows. So UI font sizes are authored as **canvas fractions** in
`config/fonts.json` (body/label), resolved to pixels via `fraction × hud::scale`
each time the window resizes. The engine's resize callback (`gameOnResize`)
reloads the role fonts at the new size and re-points the HUD systems at the fresh
handles. `FontManager::loadFont` caches by (face, size) so the reload is cheap and
leak-free (it returns the existing handle for a size already baked). Bitmap-atlas
crispness is preserved — a font is re-baked at the new pixel size, never rescaled.

## Visibility modes (Elden-Ring style — anti-crowd)

To keep the contemplative register uncluttered, HUD elements default to fading in
only when relevant. Three modes (global, later per-element), like Elden Ring:

- **Auto** *(default)* — each element **fades in when relevant, out when idle**:
  the reading/thought box on observe, the menu on a spot, a toast on an event,
  and any always-on element (future: a clock) shows only when it changes or the
  player summons it. At rest the screen is **empty** — just the world. This is
  essentially what the box + toasts already do; Auto formalizes it as the mode and
  extends it to anything persistent.
- **On** — region chrome stays visible even when idle (a persistent frame; the
  future home of always-on elements like a clock). Content fills it when present.
- **Off** — no HUD regions draw at all.

The mode is a real value now: `hud::Visibility {Auto, On, Off}`, authored in
`config/hud.json` (`"visibility"`, default `auto`) and carried on
`GameState::hud`. The render loop reads it — Off suppresses HUD drawing, On adds
idle region frames under the content, Auto draws only regions that hold content.
Building the enum first means the future Settings toggle only has to flip the
value, not introduce the concept.

The mode toggle's **UI** lives in a **Settings sub-view under the pause System
tab** (alongside Controls, before Quit) — the future home for HUD mode, and later
resolution/fullscreen, audio volumes, etc. That Settings surface **doesn't exist
yet** (System has only Controls + Quit), so the toggle is set from config until
Settings is built. Fixed regions + Auto compose cleanly: a region simply isn't
drawn when it has nothing to show (see empty-region chrome below).

## What this changes vs. today

- ThoughtBox stops computing `boxW/boxH` from content; it reads a region rect from
  config and lays content out inside it (paginating if needed).
- The `PendingLine::kind` routing picks THOUGHT vs OBSERVATION region (already the
  styling signal — now also the placement signal).
- Notify anchors to the OBSERVATION-region top (already level-with-box-top; the
  region formalizes that).
- `config/hud.json` becomes the single source for all HUD geometry (region rects,
  padding, chrome). `boxTopFrac()` folds into it.

## Resolved (the build spec)

- **Reading vs menu in the lower region** → the menu **replaces** the reading. One
  thing at a time in the lower band: read → dismiss → the action menu appears in
  the same region. No co-display (avoids crowding); matches the current flow.
- **Empty-region chrome** → a region draws **only when it holds content**. At rest
  the screen is just the world — no persistent empty frames. This is the Auto
  visibility default (see Visibility modes).
- **Page-turn affordance** → a subtle **▾** at the region's bottom-center when a
  next page exists; it disappears on the last page (where Space dismisses).
- **Region rects** → first-pass fractions authored in `config/hud.json`, tuned by
  feel after first run. First pass (of the 16:9 canvas): THOUGHT band ≈ top-center,
  y 0.08–0.34, ~0.60 wide; OBSERVATION/ACTION band ≈ bottom-center, y 0.66–0.92,
  ~0.60 wide; NOTIFICATION ≈ center-right, top edge at the OBSERVATION band top,
  stacking upward. Nothing load-bearing rides on the exact numbers.

## Build order (dependencies)

1. **Fullscreen + reference-canvas / anchor scaling** (Dimensions) — the
   foundation. Must come first, or region rects get built in raw pixels and redone
   per resolution.
2. **Fixed regions** — the three regions render into their reference-canvas rects
   (kind-routed), pagination, empty-only chrome. Sits on #1.
3. **Auto visibility** — hardcoded behavior in the first build (essentially what
   the box + toasts already do).
4. *(Later)* **Settings sub-view under System** → the **On/Off** visibility toggle
   (+ future resolution/audio settings). Prerequisite: the Settings surface, which
   doesn't exist yet.

## Cross-references

[PROCESSING-MODEL.md](PROCESSING-MODEL.md) (thoughts/observations, `PendingLine::kind`) ·
[ACTIONS.md](ACTIONS.md) (the action menu) · [AESTHETIC.md](AESTHETIC.md) (register)
