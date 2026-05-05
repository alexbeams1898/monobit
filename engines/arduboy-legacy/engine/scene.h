// Scene dispatch: vtable-indirected update/draw entry points.
//
// Background: docs/scene-paging.md, .claude/rules-scenes.md.
//
// At runtime, exactly one SceneVTable is "active" — its update/draw
// fnptrs are what main() calls every frame. Switching scenes
// (TITLE → MAIN_MENU → GATE → PLAY → ...) means installing a new
// vtable. Today (pre-paging) this is just a fnptr indirection that
// defeats LTO inlining; post-paging, scene::switch_to() will also
// SPM-page the scene's bytes into internal flash from FX before
// installing the vtable.
//
// The vtable is `volatile`-pointed at runtime so LTO can't statically
// resolve which scene's functions are called. Without volatile, GCC
// observes that current_vtable is only ever set to a small set of
// addresses and inlines the dispatch back into main, defeating the
// whole point of the indirection.
//
// CORE-resident: yes. The vtable struct, the active pointer, and
// the dispatch are all in `.text` (CORE) by construction — `main()`
// must be able to call them regardless of which scene is paged in.

#pragma once

#include "types.h"

// Place a function/data symbol into a named scene bank. The linker
// script collects all symbols matching `.scene.<NAME>.*` into one
// output section, which lands in the swap region. NAME is one of:
//   TITLE, MAIN_MENU, GATE, PLAY  (matches scene::Id below).
//
// Three macros, picked by role:
//
// SCENE_ENTRY(NAME) — for the update/draw functions called via the
//   volatile vtable fnptr. MUST be noinline (otherwise scene_audit
//   can't see the per-scene call graph and we lose measurement) and
//   MUST be in the right section. Used on the eight functions
//   referenced by VT_TITLE / VT_MAIN_MENU / VT_GATE / VT_PLAY plus
//   their on_enter / on_leave hooks if any.
//
// SCENE_FN(NAME) — for any other function whose body should live in
//   the scene bank. Section + used, but lets GCC inline if profitable.
//   When GCC inlines such a function, the bytes vanish into the
//   caller (which is itself in .scene.NAME if tagged), so paging is
//   unaffected. When GCC keeps it separate, it lands in the right
//   section.
//
// SCENE_DATA(NAME) — for PROGMEM data tables only used by one scene.
//
// `used` is load-bearing: many scene-bank symbols are reachable only
// via vtable fnptr, which --gc-sections can't see. Without `used` the
// linker would strip them.
//
// On SDL builds the section attributes are harmless (the host linker
// places the section like any other) and there's no swap manager —
// the whole binary stays resident.
#define SCENE_ENTRY(NAME) __attribute__((noinline, used, section(".scene." #NAME)))
#define SCENE_FN(NAME)    __attribute__((used, section(".scene." #NAME)))
#define SCENE_DATA(NAME)  __attribute__((used, section(".scene." #NAME)))

namespace scene {

// Stable identifiers for each scene bank. `set_active` takes one of
// these (post-paging it'll page in the corresponding bytes from FX).
// Today (pre-paging) the ID is informational; the active scene is
// inferred from the vtable pointer.
enum Id : u8 {
  ID_TITLE     = 0,
  ID_MAIN_MENU = 1,
  ID_GATE      = 2,
  ID_PLAY      = 3,
  ID_COUNT,
};

struct VTable {
  void (*update)();
  bool (*draw)();      // returns true if framebuffer changed (= flush needed)
  void (*on_enter)();  // optional; null if not used
  void (*on_leave)();  // optional; null if not used
};

// (Earlier design used `SceneEntryOffsets` — a struct of byte-offsets
// resolved against `__scene_bank_start` at runtime. That required
// hand-rolled AVR fnptr encoding and turned out to be brittle. The
// generator now emits a `scene_vtables[ID_COUNT] PROGMEM` table whose
// entries are real `VTable`s with linker-resolved symbol fnptrs —
// see tools/fxdata/scene_extract.py.)

// The currently-active scene's entry points. main() reads this every
// frame and dispatches through it. `volatile` is load-bearing — it
// blocks GCC from constant-propagating which vtable is in effect and
// inlining the dispatch back into main(), which would defeat the
// entire indirection. (Tested: without volatile, -flto hoists the
// switch into main and main() goes back to ~10 KB.)
//
// Post-paging: `current` points at a CORE-resident vtable struct
// (`active_vtable_storage` in scene.cpp) whose four fnptrs are
// REWRITTEN after each swap to point at the bank-resident scene's
// entry points. The static VT_TITLE / VT_MAIN_MENU / VT_GATE / VT_PLAY
// definitions in game.cpp are used pre-paging only; under paging, the
// only thing the application sees is `current` (which points at the
// active-vtable storage, refreshed per swap).
extern const volatile VTable* current;

// Identifier for the currently-resident scene bank. Updated by
// set_active. Pre-paging: ID_COUNT (unused). Post-paging: one of
// ID_TITLE..ID_PLAY.
extern u8 current_id;

// Install a new vtable + scene id. Pre-paging: just sets `current`
// + calls on_leave/on_enter. Post-paging: if `id != current_id`,
// page the new scene's bytes from FX into the internal-flash bank
// via kp_boot_32u4's SPM trampoline, then repopulate the active
// vtable storage from the per-scene entry-offset table, then call
// on_enter.
//
// The legacy 1-arg form (`set_active(vt)`) is preserved for the
// pre-paging build — it passes ID_COUNT, which the swap path treats
// as "don't page".
void set_active(const VTable* vt, u8 id = ID_COUNT);

}  // namespace scene
