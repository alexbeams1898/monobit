# crash_boundary — watchdog tombstone

Drop-in error boundary for the Arduboy build. When the main loop wedges
(infinite loop, jumps to garbage, smashed return address — anything that
stops the loop from petting the watchdog), the AVR resets and on next
boot the game shows a tombstone screen with the breadcrumb of the prior
run's last live state:

```
THOU HAST PERISHED
─────────────────
S:7   V:1
C:1   F:2841
─────────────────
A:RISE AGAIN
```

That's `state=7` (SHADES_SCREEN), `sub_state=1` (SHADES_DETAIL),
`cursor=1`, frame `2841 mod 65536`. Press A to dismiss and return to the
title.

## When to use

Wire it back in any time the game starts hanging or rebooting silently
on hardware/Ardens. The breadcrumb tells you exactly which screen +
sub-state + cursor was live when the loop died — enough to reproduce
deterministically.

## When NOT to use

The watchdog only catches **wedges** (loop stops). It does NOT catch
clean logic bugs where the loop is alive but rendering wrong. For
those, plant an on-screen diagnostic banner instead — see
`draw_diag_banner()` in earlier game.cpp commits or just write a fresh
one. The "hot blood river" stack-smash bug
(see `.claude/CLAUDE.md` → "PROGMEM stack-buffer trap") was an example
of the second kind: WDT never fired, banner found it.

## Cost

- **Flash:** ~390 B
- **RAM:** 9 B (8 B `.noinit` Context + 1 B `.noinit` MCUSR cache)

Both fit comfortably under the gates with current footprint headroom.

## Files in this directory

- `crash.h` — engine-portable API the game writes to.
- `crash_arduboy.cpp` — AVR plumbing: `.init3` MCUSR capture, `.noinit`
  breadcrumb, watchdog enable/pet/disable.
- `crash_arduboy_priv.h` — platform-private API (`arm_watchdog_1s`,
  `pet_watchdog`) the AVR `main()` calls.

## Redeploy recipe

Five steps. Each step lists the file to edit and what to insert. Build
should green at the end (~+390 B flash, +9 B RAM).

### 1. Copy source files into the build tree

```sh
cp tools/crash_boundary/crash.h                  engine/crash.h
cp tools/crash_boundary/crash_arduboy.cpp        platform/arduboy/crash.cpp
cp tools/crash_boundary/crash_arduboy_priv.h     platform/arduboy/crash_avr.h
```

The Makefile globs `engine/*.cpp` and `platform/arduboy/*.cpp`, so no
Makefile edit is needed.

### 2. Wire the platform main loop

In `platform/arduboy/main.cpp`, add the includes and arm/pet/record
calls. Diff:

```cpp
 #include "audio.h"
 #include "clock.h"
+#include "crash.h"
+#include "crash_avr.h"
 #include "display.h"
 ...

 int main() {
   display::init();
   clock::init();
   audio::init();

+  // game::init() reads crash::had_watchdog_reset() to decide whether to
+  // boot into TOMBSTONE instead of TITLE — so it MUST run before we arm
+  // the WDT (the breadcrumb is already captured in .init3 long before main).
   game::init();
+  crash::arm_watchdog_1s();

   for (;;) {
     perf::frame_begin();
+    crash::pet_watchdog();

     input::poll();
     ...
     audio::tick();

+    // Record live state BEFORE update so a wedge inside update is captured
+    // with the state we believed we were in when entering it.
+    crash::record(game::breadcrumb_state(), game::breadcrumb_sub(),
+                  game::breadcrumb_cursor(), (u16)clock::frame_count);
+
     perf::scope_begin(PERF_UPDATE);
     game::update();
```

### 3. Add the breadcrumb accessors to game.h

```cpp
 #pragma once
+
+#include "types.h"

 namespace game {

 void init();
 void update();
 bool draw();
+
+u8 breadcrumb_state();
+u8 breadcrumb_sub();
+u8 breadcrumb_cursor();

 }  // namespace game
```

### 4. Wire the game layer

In `games/rpg/game.cpp`:

**a. Add include:**
```cpp
 #include "clock.h"
+#include "crash.h"
 #include "direction.h"
```

**b. Add TOMBSTONE to the State enum** (last entry):
```cpp
   SECOND_DEATH,
+  TOMBSTONE,    // shown on cold boot iff the watchdog rebooted the chip;
+                // displays the breadcrumb of the prior run's last live state
 };
```

**c. Boot routing in `init()`:**
```cpp
 void init() {
   best = storage::read_best_run();
   meta = storage::read_meta();
-  state = TITLE;
+  if (crash::had_watchdog_reset() && crash::last_context().magic == 0xDEAD) {
+    state = TOMBSTONE;
+  } else {
+    state = TITLE;
+  }
 }
```

**d. Tombstone input in `update()`** (very top, before TITLE):
```cpp
 void update() {
+  if (state == TOMBSTONE) {
+    if (input::pressed(input::A)) state = TITLE;
+    return;
+  }
+
   if (state == TITLE) {
```

**e. Dirty-policy switch in `draw()`** (add TOMBSTONE to on-entry group):
```cpp
   case TITLE_RETURN:
   case CIRCLE_CARD:
+  case TOMBSTONE:
     dirty = (state != last_drawn_state);
     break;
```

**f. Dispatch switch in `draw()`** (add TOMBSTONE case):
```cpp
   bool handled = true;
   switch (state) {
+  case TOMBSTONE: draw_tombstone(); break;
   case TITLE: draw_title(); break;
```

**g. Add `draw_tombstone()` near `draw_title()`:**
```cpp
void draw_tombstone() {
  fb::clear();
  const crash::Context& c = crash::last_context();
  font::draw_text(28, 4, "THOU HAST PERISHED");
  fb::fill_rect(0, 12, fb::WIDTH, 1);
  font::draw_text(8, 22, "S:");
  font::draw_uint(40, 22, (u16)c.state);
  font::draw_text(64, 22, "V:");
  font::draw_uint(96, 22, (u16)c.sub_state);
  font::draw_text(8, 32, "C:");
  font::draw_uint(40, 32, (u16)c.cursor);
  font::draw_text(64, 32, "F:");
  font::draw_uint(120, 32, c.frame_lo);
  fb::fill_rect(0, 50, fb::WIDTH, 1);
  font::draw_text(28, 55, "A:RISE AGAIN");
}
```

**h. Add the breadcrumb accessor implementations** (just before the
final `}  // namespace game`):
```cpp
u8 breadcrumb_state() {
  return (u8)state;
}
u8 breadcrumb_sub() {
  switch (state) {
  case SHADES_SCREEN: return shades_view;
  case TEXT_SCREEN: return text_section;
  case PAUSED: return pause_confirming;
  case TUTORIAL: return tutorial_slide;
  default: return 0;
  }
}
u8 breadcrumb_cursor() {
  switch (state) {
  case SHADES_SCREEN: return shades_cursor;
  case NUMERALS_SCREEN: return numerals_cursor;
  case LEXICON_SCREEN: return lexicon_cursor;
  case TEXT_SCREEN: return text_cursor;
  case NAME_ENTRY: return name_cursor;
  case UPGRADE_MENU: return upgrade_cursor;
  case MAIN_MENU:
  case GUIDE_SCREEN: return menu_index;
  default: return 0;
  }
}
```

(Adjust the cases if the state enum or sub-state globals have evolved.)

### 5. Build and test

```sh
make
```

Should compile clean. Verify in Ardens:
1. Press F5 — game boots normally to TITLE (no tombstone, since this is
   a power-on reset, not a watchdog reset).
2. To force a wedge for testing, drop a `for(;;){}` infinite loop into
   any input handler, build, run, trigger that input, wait ~1 second.
   The chip should reset and boot into the tombstone screen with the
   correct `(state, sub_state, cursor, frame)` for the wedge site.

## Removal

Reverse the recipe:

```sh
rm engine/crash.h
rm platform/arduboy/crash.cpp
rm platform/arduboy/crash_avr.h
```

…then strip every `crash::` reference and the TOMBSTONE state from
`game.cpp`, `game.h`, and `main.cpp`. Build should green and footprint
drop ~390 B flash + 9 B RAM.
