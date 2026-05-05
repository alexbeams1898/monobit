# Scene paging

Status: **shipped on the `scene-split-d` branch.** Verified end-to-end
in Ardens. Hardware verification still pending (requires kp_boot_32u4
bootloader installed on the device).

Recovers ~14 KB of internal flash by keeping only one of four scene
banks resident at a time, with the others stored as cold copies on FX
flash and loaded via SPM page-write through a bootloader trampoline.

## TL;DR runbook

Most of what's below is architecture detail. This section is the part
to remember when coming back to this code months later.

### Building

```
# Standard non-paging build (everything in flash, no bootloader needed):
make all

# Paging build for hardware (requires kp_boot_32u4 installed; see
# "Hardware deployment" below):
make paging-build PAGING=1

# Paging build + launch in Ardens (in-app SPM stub, no bootloader
# install needed in the simulator):
make paging-ardens
```

`paging-build` orchestrates the three-pass pipeline:
1. Compile with stub `scene_entries.cpp` (all-nullptr table) → ELF.
2. Extract real `scene_entries.cpp` from that ELF + rebuild → FINAL ELF.
3. Extract cold copies from FINAL ELF, bundle into `data.bin`, rebuild
   to refresh `scene_offsets.h`. (The cold copies MUST come from the
   final ELF — extracting from pass-1 produces lds-address mismatches
   between cold bytes and what the deployed `.hex` expects, surfacing
   as post-swap EEPROM-OOB.)

Final artifacts:
- `build/rpg-arduboy-release/rpg.hex` — flashable image (~14 KB,
  TITLE bank only).
- `build/rpg-arduboy-release/rpg.stripped.elf` — what the .hex was
  made from; useful for `avr-objdump`.
- `build/fxdata/data.bin` — manifest assets + 4 scene cold copies.
- `build/fxdata/scene_offsets.h` — FX offsets/sizes per scene
  (generated; included by `platform/arduboy/scene_paging.cpp`).
- `build/fxdata/scene_entries.cpp` — generated `scene_vtables[]`
  table; compiled into the binary.

### Adding a new screen

See `.claude/rules-scenes.md` for the discipline. Quick version:

1. Pick a bucket: TITLE / MAIN_MENU / GATE / PLAY (where MAIN_MENU =
   navigable menu tree, GATE = pre-run cinematic, PLAY = gameplay
   incl. PAUSED + SECOND_DEATH).
2. Tag the new function `SCENE_FN(BUCKET)` (helpers) or
   `SCENE_ENTRY(BUCKET)` (vtable update/draw entry points).
3. Add the dispatch case in the bucket's `update_<bucket>_scene()` /
   `draw_<bucket>_scene()` body.
4. Update the State enum + `VTABLE_BY_STATE` + `SCENE_ID_BY_STATE`
   tables in `games/rpg/game.cpp`.
5. `make audit-all` should report 0 cross-bank edges.

### Hardware deployment

Devices need the **kp_boot_32u4** bootloader installed (a 1 KB
USB-DFU bootloader exposing an SPM trampoline at flash 0x7FF0;
https://github.com/ahtn/kp_boot_32u4, MIT). Stock Arduboys ship with
Caterina, which doesn't expose SPM to the application.

One-time per device, no extra hardware required (kp_boot_32u4 ships
with a Pro-Micro-style "convert via Arduino IDE" sketch that uses
Caterina's own self-flashing to replace itself).

After bootloader install: standard USB flash of `rpg.hex` works for
all future game updates. The application calls SPM via the bootloader
trampoline at 0x7FF0 — installed once, used for every scene swap.

### Debugging in Ardens

Ardens doesn't model the silicon's "SPM-from-app section is disabled"
restriction (verified by reading Ardens'
`src/absim_atmega32u4.hpp:execute_spm` — there's no PC-source check).
We exploit this with `ARDENS_SPM=1`: the SPM trampoline call gets
redirected to an in-app stub at `app_spm_trampoline_entry`
(`platform/arduboy/spm_app_stub.cpp`). This lets us test the swap
manager in Ardens without a custom bootloader installed in the
simulator.

**Critical: an Ardens-passing build is NOT proof of hardware
correctness.** The silicon-level SPM-from-app restriction is real
even though Ardens doesn't enforce it. Hardware test is the only way
to confirm the trampoline path works.

### Common failures

- **"FLASH GATE FAILED" past 28,672 B on a paging build** — the gate
  is reading the un-stripped ELF (which has all four banks). Run
  with `PAGING=1` so the gate uses `$(STRIPPED_ELF)`.
- **Black screen at boot** — the very first `set_active` call: bank
  bytes don't match what the entry-point fnptrs expect. Check that
  cold copies came from the FINAL ELF, not pass 1.
- **EEPROM-OOB after a swap** — same root cause: cold-copy bytes ran
  against a different `.text` layout than the deployed `.hex`. The
  `paging-build` Make target handles this by re-extracting from the
  final ELF; manual builds need to do the same.
- **Fart noise during scene transitions** — `audio::mute()` should
  be called at the top of `platform_scene_page_in`. Audio ISR halts
  for ~400 ms during SPM page-writes; whatever sample is in the DAC
  gets driven onto the speaker pin for the entire halt window.
- **Visible freeze when A-skipping wood transition** — the swap is
  hitching. Filed as a follow-up: incremental swap across frames so
  the transition continues at half-speed instead of freezing.

### Files to touch

| To change... | Edit |
|---|---|
| Add a screen | `games/rpg/game.cpp` (state, dispatch) + `.claude/rules-scenes.md` |
| Add a bucket | spec rewrite + linker script + scene_extract.py + Makefile + game.cpp |
| Tweak the SPM page loop | `platform/arduboy/scene_paging.cpp` |
| Change the SPM trampoline ABI | `platform/arduboy/kp_boot_spm.[h,cpp]` (and the bootloader) |
| Move bank base / size | `platform/arduboy/scene.ld` |
| Change the cold-copy build | `tools/fxdata/build.py` + `tools/fxdata/scene_extract.py` |
| Change the dispatcher (vtable) | `engine/scene.h` + `engine/scene.cpp` |

## Why

The Caterina-bound flash ceiling is **28,672 B** (32 KB chip − 4 KB
bootloader). Post-de-inline, our build is **28,494 B**, leaving **178 B
of headroom**. Every feature past this point pushes us over.

Scene-paging trades runtime hitch (~1.5 s on coarse scene transitions)
for flash headroom. Per the post-de-inline scene_audit:

| Scene     | Bytes  | %     |
|-----------|-------:|------:|
| CORE      | 16,818 | 59.8% |
| MENUS     |  6,150 | 21.9% |
| CUTSCENES |  2,979 | 10.6% |
| PLAYING   |  2,178 |  7.7% |

Cross-scene call edges between pageable scenes: **0**.

After paging + dispatcher rewrite, projected CORE: ~8–10 KB. Projected
flash recovery: ~6–8 KB of internal-flash headroom.

## Scope

**Page everything that can be paged.** The exhaustive list of what
stays CORE:

1. **ISR path.** `audio::tick`, `audio::step_voice_state`, `audio::emit_voice`,
   all `__vector_*` trampolines, anything they transitively call. The
   audio ISR fires every 62.5 µs (16 kHz, see `platform/arduboy/audio.cpp:117`);
   it cannot tolerate a "not currently mapped" function.
2. **Swap manager itself.** `engine/scene.cpp` — `scene::switch_to()`,
   the FX→SPM page loop, the vtable, `current_scene` state. By
   construction can't page itself out.
3. **Shared primitives.** `fb::*`, `font::*`, `input::*`, `data_flash::read`,
   `lz77::decode*`, `clock::*`, `display::*`, `storage::*` (EEPROM),
   `sprites::width/height/data/fx_offset/...`, `tilemap::*`, `ent::*`
   (entity pool — used by PLAYING + Reckoning portrait). Cross-bank
   calls during a swap deadlock; these have to be reachable always.
4. **`main` post-refactor.** ~50 B trampoline (see "Dispatcher rewrite"
   below). Reads `state`, looks up the active vtable's `update`/`draw`
   fnptrs, calls them.

**Everything else pages.** Including PAUSED, SECOND_DEATH, scene init/
teardown, scene-local PROGMEM tables, scene-local statics. PAUSED
hitching ~1.5 s on first open is the cost of "the correct
implementation"; see "Hitch budget" for why we accept it.

## Scene buckets

Four scenes. Each is a self-contained TU; transitions between scenes
within a bucket are zero-swap (cursor moves, dirty-cache hits), only
inter-bucket transitions trigger SPM.

| ID                | States covered                                          | Cold-copy size estimate |
|-------------------|---------------------------------------------------------|------------------------:|
| `SCENE_TITLE`     | TITLE                                                   | ~600 B                  |
| `SCENE_MAIN_MENU` | MAIN_MENU, NAME_ENTRY, UPGRADE_MENU, STATS_SCREEN, SHADES_SCREEN, NUMERALS_SCREEN, LEXICON_SCREEN, TEXT_SCREEN, TUTORIAL, GUIDE_SCREEN | ~6,800 B |
| `SCENE_GATE`      | GATE_CARD, CIRCLE_CARD                                  | ~2,400 B                |
| `SCENE_PLAY`      | PLAYING, PAUSED, SECOND_DEATH                           | ~3,000 B                |

Sizes are estimates from current SCENE_RULES classifications.
Locked-in numbers come from the scene_audit run after the dispatcher
refactor (todo step 4).

The `SCENE_PLAY` bucket includes PAUSED + SECOND_DEATH because
transitions between them are gameplay-internal — pause↔play has to be
zero-swap or pause feels broken.

## Architecture

Single bank, FX-backed, vtable-dispatched.

### Internal flash layout

```
0x0000 ─────────────────── application section (bootloader fuses target this)
       │ CORE code + data │
       │ + main() trampoline
       │ + scene::* swap manager
       │ + always-resident vtable
       │
       ├──────────────────  __scene_bank_start (linker symbol)
       │ swap region       │  ~7 KB, sized to fit the largest scene
       │ ".scene.active"   │   bucket cold-copy (likely SCENE_MAIN_MENU)
       │                   │  rounded up to 128 B page boundary
0x6F00 ├──────────────────  __scene_bank_end
       │   (unused)        │  if any
0x7000 ─────────────────── Caterina bootloader (4 KB, 0x7000–0x7FFF)
0x7FFF ───────────────────
```

The swap region is one contiguous range. At any moment it holds the
currently-resident scene's code + data. Swap = SPM-erase the whole
range, SPI-stream new contents from FX.

### FX (data.bin) layout — additions to the existing manifest

Today `tools/fxdata/build.py` concatenates assets named in
`tools/fxdata/manifest.txt` and emits `OFFSET_<NAME>` /
`OFFSET_<NAME>_SIZE` constants in `data_offsets.h`. Extend it with a
parallel scene table.

Generated `build/fxdata/data.bin` after extension:

```
[ existing assets: sprites, gates, logos, etc. ]
[ scene cold copies: padded to 128 B boundary each ]
  SCENE_TITLE bytes
  SCENE_MAIN_MENU bytes
  SCENE_GATE bytes
  SCENE_PLAY bytes
[ trailing 0xFF padding to 16 MB − 4 KB ]
```

Generated `build/fxdata/scene_offsets.h`:

```cpp
namespace scenes {
constexpr u32 OFFSET_TITLE = 0x...;
constexpr u16 SIZE_TITLE   = 0x...;  // bytes, including any final-page slack
constexpr u8  PAGES_TITLE  = 0x...;  // SIZE / 128, ceiling
// ...one set per scene...
}
```

### Per-function section attribution (single-file approach)

The original spec called for one TU per scene (`scene_title.cpp`,
`scene_main_menu.cpp`, etc.). On the second pass, that's deferred —
splitting the TU exposes every cross-TU shared global (~300 lines of
header plumbing) and the file boundary doesn't carry information the
linker can't already get from per-function section attributes. So:

**Each scene-bucket function and data in `games/rpg/game.cpp` is tagged
individually with a `SCENE_FN` / `SCENE_DATA` macro that emits an
`__attribute__((section(".scene.<name>")))` attribute.**

```cpp
// games/rpg/game.cpp
SCENE_FN(MAIN_MENU) static void draw_main_menu() { ... }
SCENE_FN(MAIN_MENU) static void update_main_menu_scene() { ... }
SCENE_DATA(MAIN_MENU) static const char T_TOP_0[] PROGMEM = "COMMANDS";
// ...
```

`-ffunction-sections -fdata-sections` (already on in our Makefile)
means each function/data already gets its own section
(`.text.draw_main_menu`, etc.); the SCENE_FN / SCENE_DATA macro
overrides the section name to one the linker can collect into the
swap bank.

**Note on intermediate state:** between the tagging pass and the
linker-script pass, `.scene.*` sections live in the application
section just past `.text` and DO count against the 28,672 B ceiling.
`avr-size`'s `Program:` line silently undercounts (it sums only
`.text + .data + .bootloader`), so `scripts/check_flash.py` was
extended to walk every CODE+ALLOC+LOAD section via `avr-objdump -h`.
The real flash savings only materialize once the linker script
relocates the bank to its dedicated region AND the cold copies are
deferred to FX flash (no in-app duplicate).

**Why per-function instead of per-TU:**
- No need to promote anonymous-namespace globals to a shared header.
- No risk of half-migrated state living in two TUs at once.
- A new lint rule (`scene-fn-coverage`) walks the call graphs of the
  four `update_*_scene` / `draw_*_scene` entry points and verifies
  every function reachable transitively from them carries SCENE_FN
  with the matching tag. Catches drift better than file boundaries.
- The TU split can still happen later as pure code organization if
  `game.cpp` gets unwieldy. It's downstream of paging, not upstream.

A `SCENE_FN(NAME)` and `SCENE_DATA(NAME)` macro in `engine/scene.h` wrap
the raw attribute and force `used` (so the linker can't dead-strip
data referenced only from paged code via fnptr). The volatile-fnptr
indirection through `scene::current` already prevents inlining, so
SCENE_FN doesn't need to add `noinline`.

### Linker script

New file: `platform/arduboy/scene.ld` (linker fragment, included via
`-T` after the default avr5 script). Defines:

- `__scene_bank_start = N` (a symbol pinned to a chosen page boundary
  in the application section)
- A `.scene.active` output section at `__scene_bank_start`, with all
  `.scene.*` input sections consolidated into it. **At link time only
  one scene's input section is fed in** — the boot-resident scene
  (SCENE_MAIN_MENU; the title fades to it on cold boot). The other
  three scenes' object files are linked separately into a parallel
  `.scene-coldX.elf` artifact whose contents become FX cold copies
  (see "Cold-copy generation" below).
- `__scene_bank_size` = `__scene_bank_end - __scene_bank_start`,
  exposed to C++ for the swap manager's page count.
- A KEEP() rule on the `.scene.active` section to prevent
  `--gc-sections` from stripping vtable entries (which are referenced
  only via the vtable struct, not directly).

### Cold-copy generation

`tools/fxdata/build.py` is extended with a `--scene-elfs` mode that:

1. Takes the four per-scene linked artifacts as input
   (`build/.../scene_title.elf`, etc. — produced by separate link
   targets in the Makefile).
2. For each, runs `avr-objcopy -O binary --only-section=.scene.<name>`
   to produce a flat byte image.
3. Pads each to a 128 B boundary.
4. Concatenates them into `data.bin` after the existing assets.
5. Emits `scene_offsets.h` with the `OFFSET_*`, `SIZE_*`, `PAGES_*`
   triple per scene.

The boot-resident scene (SCENE_MAIN_MENU) does NOT need a cold copy on
FX — it's already present in internal flash at boot. But we emit one
anyway, so the swap manager can re-page it into the bank after a
detour through SCENE_PLAY.

### Dispatcher rewrite (CORE side)

```cpp
// engine/scene.h
struct SceneVTable {
  void (*update)();
  void (*draw)();
  void (*on_enter)();   // run after a swap completes
  void (*on_leave)();   // run before a swap begins
};

namespace scene {
  extern SceneVTable current_vtable;     // CORE-resident, written by switch_to()
  extern u8 current_id;                   // SCENE_TITLE / SCENE_MAIN_MENU / ...
  void switch_to(u8 scene_id);            // does nothing if already resident
}
```

`main()` post-refactor (lives in `platform/arduboy/main.cpp`,
unchanged in shape, but the body shrinks):

```cpp
int main() {
  display::init();
  audio::init();
  storage::init();
  game::init();                          // global state init only; no scene
  scene::switch_to(SCENE_TITLE);         // initial paging
  for (;;) {
    input::poll();
    clock::tick_frame();
    scene::current_vtable.update();      // dispatched, scene-local
    scene::current_vtable.draw();        // dispatched, scene-local
    display::blit(fb::buffer);
  }
}
```

Each scene TU registers its vtable via a CORE-resident table indexed
by `scene_id` (CORE-resident because it has to be readable while the
swap is being staged):

```cpp
// engine/scene.cpp
namespace scene {
  // Filled at link time: the .scene.* TUs export these symbols, and
  // the linker resolves them from whichever copy is currently mapped.
  extern "C" void scene_update();        // weak alias resolved per-bank
  extern "C" void scene_draw();
  extern "C" void scene_on_enter();
  extern "C" void scene_on_leave();
}
```

After a swap, `switch_to()` reads the four entry-point addresses from
a 16 B header at the start of the cold copy (see "Per-scene header"
below) and writes them into `current_vtable`. The vtable is the only
indirection `main()` does — the bank itself is at a known address but
its symbol table changes per scene.

### Per-scene header

Every scene's cold copy starts with a 16 B header, generated by the
linker (or by the cold-copy build step):

```
offset  size  field
0x00    2     magic (0xCE5C — "SCEN" rotated)
0x02    1     scene_id
0x03    1     reserved
0x04    2     update_offset    // bytes from bank base
0x06    2     draw_offset
0x08    2     on_enter_offset
0x0A    2     on_leave_offset
0x0C    4     reserved (CRC of the rest of the bytes; checked on swap)
```

`switch_to()` reads this header from FX before erasing the bank, so it
knows what to write to `current_vtable` post-swap.

### Swap manager

```cpp
// engine/scene.cpp
void scene::switch_to(u8 new_id) {
  if (new_id == current_id) return;

  // Drain any per-scene teardown.
  if (current_vtable.on_leave) current_vtable.on_leave();

  // Read the new scene's header from FX.
  SceneHeader hdr;
  data_flash::read(scenes::OFFSET[new_id], &hdr, sizeof(hdr));
  // (Validate magic; if it fails, we have corrupted FX — halt + show
  //  on-screen "FX CORRUPT, REFLASH" rather than running garbage code.)

  // Critical section. Disable interrupts for the duration; audio will
  // glitch (~1.5 s of dead silence). The user sees a transition card
  // (gate fade, wood transition, etc.) so this is masked diegetically.
  cli();
  spm_erase_bank();                                 // ~3.5 ms × N pages
  spm_write_from_fx(scenes::OFFSET[new_id] + 16,    // skip header
                    scenes::PAGES[new_id]);
  sei();

  // Wire up the new vtable.
  current_vtable.update    = bank_addr(hdr.update_offset);
  current_vtable.draw      = bank_addr(hdr.draw_offset);
  current_vtable.on_enter  = bank_addr(hdr.on_enter_offset);
  current_vtable.on_leave  = bank_addr(hdr.on_leave_offset);
  current_id = new_id;

  if (current_vtable.on_enter) current_vtable.on_enter();
}
```

### Platform: SPM page-erase + page-write

**Architectural correction (post-research):** the original spec
assumed in-application SPM works on ATmega32u4. Microchip
documentation and AVR109 say otherwise:

> "The Application section can never store any Boot Loader code since
> the Store Program Memory (SPM) instruction is disabled when executed
> from the Application section." — Microchip AVR Read-While-Write docs

> "The SPM instruction can only be executed from the Boot Loader
> section." — AVR109 (doc1644)

Stock Caterina does NOT expose an SPM trampoline to the application
(it only programs flash during its own USB session). So scene paging
on this hardware **requires a custom bootloader** that exposes a
fixed-address SPM helper the application can call.

**Bootloader requirement:** kp_boot_32u4 (or equivalent, e.g.
Optiboot's `do_spm` style) — a 1–4 KB bootloader at 0x7000+ that
exports an SPM trampoline. Reference:
https://github.com/ahtn/kp_boot_32u4. One-time flash per device.

**Swap manager interface:**
- Application loads page address + page buffer pointer into registers.
- Calls bootloader trampoline at a fixed BLS entry address.
- Trampoline does SPM erase/fill/write/RWWSRE sequence, returns.
- Application reads back to verify.

The RWW/NRWW distinction governs whether the CPU halts during the
write. SPM from BLS (NRWW) writing to RWW (application section, where
our swap bank lives) **does NOT halt the CPU** — the bootloader runs,
the application page-write proceeds, and ISRs in the NRWW section
keep running. ISRs in the RWW section pause until the page completes.

This affects the audio plan: **the audio ISR vector + ISR body must
live in NRWW** (boot section) for audio to keep playing during a
swap. Today they're in `.text` (RWW). Either:
- (a) Move audio ISR to a tiny NRWW shim in our custom bootloader.
- (b) Accept ~400 ms of audio silence per swap (still hidden under
  transitions; documented in the spec already).

(b) is simpler, ships faster. Revisit (a) only if audio glitch is
unacceptable on real hardware.

### Page timing

SPM disables interrupts implicitly during the erase and write phases
(~3.5 ms erase, ~4.5 ms write per 128 B page). For a ~6 KB scene =
48 pages, total swap budget is roughly:

- 48 × 3.5 ms erase = 168 ms
- 48 × 4.5 ms write = 216 ms
- 48 × ~0.3 ms FX read into RAM page buffer (fill phase) = ~14 ms
- Overhead (vtable updates, header read, etc.): ~5 ms

**Total: ~400 ms per swap.**

This is below typical "scene transition card" duration (the wood
transition is ~600 ms, gate fade is ~800 ms), so we can hide the swap
inside an existing transition without it being visually noticeable.

### Audio during swap

SPM page-write blocks the CPU for the full ~4.5 ms per page, hard
real-time. Audio ISR is 16 kHz = one tick every 62.5 µs. So audio is
silent for the duration of each page write, with brief ~150 µs
windows in between when the FX read fills the next page buffer.
Net: ~95% audio silence during the swap.

Mitigations:

1. **Cover with a transition.** The swap fires on TITLE→MAIN_MENU
   (covered by the title fade, which already plays no music) and
   MAIN_MENU→GATE (covered by the wood transition, which plays a
   silence beat already). The user never hears audio cut out mid-note.
2. **Silence the audio engine before swap.** `audio::silence_all()`
   before `cli()`. Voice envelopes settle to zero amplitude in 1–2
   frames. Post-swap, audio resumes from silence.
3. **Don't swap during PLAYING.** SCENE_PLAY is the only scene reachable
   from active gameplay; PAUSED and SECOND_DEATH are co-resident
   with PLAYING. Combat never triggers a swap.

## Hitch budget

| Transition                     | Allowed user-perceptible delay | Source of cover |
|--------------------------------|--------------------------------|-----------------|
| Boot → SCENE_TITLE (no swap)   | 0 (boot-resident)              | n/a             |
| TITLE → MAIN_MENU              | ~400 ms swap                   | title fade-out  |
| MAIN_MENU → GATE               | ~400 ms swap                   | wood transition |
| GATE → PLAY                    | ~400 ms swap                   | gate-card final beat |
| PLAY → MAIN_MENU (post-death)  | ~400 ms swap                   | second-death plaque |
| MAIN_MENU ↔ {bestiary, lex, …} | 0 (zero-swap, same scene)      | n/a             |
| PLAY ↔ PAUSED                  | 0 (zero-swap, same scene)      | n/a             |

If any swap exceeds 600 ms in measurement, that's a bug — the
transition cover won't hide it.

## Toolchain & build pipeline changes

1. **`engine/scene.h` / `engine/scene.cpp`** — vtable struct, `switch_to`,
   `current_vtable`, `current_id`, `SCENE_FN` / `SCENE_DATA` macros.
2. **`platform/arduboy/scene_spm.cpp`** — SPM wrappers (page-erase,
   page-fill from FX, page-write). Uses `<avr/boot.h>`.
3. **`platform/sdl/scene_spm.cpp`** — stub. SDL has all scenes resident;
   `switch_to` just updates the vtable + calls on_enter/on_leave.
4. **`platform/arduboy/scene.ld`** — linker fragment for `__scene_bank_*`
   and the `.scene.active` section.
5. **`tools/fxdata/build.py`** — add `--scene-elfs` mode; emit
   `scene_offsets.h`.
6. **`Makefile`** — four new link targets (one per scene cold copy),
   plus the data.bin extension. Each scene cold copy is built as a
   stand-alone link: same CORE objects, but only one scene's TU is
   pulled in, linked at `__scene_bank_start`. The cold copy is the
   `.scene.<name>` section extracted from the resulting ELF.
7. **`games/rpg/scene_*.cpp`** — four new TUs replacing the relevant
   parts of `games/rpg/game.cpp`.
8. **`games/rpg/game.cpp`** — shrinks to: global init, the global state
   variables (`storage::MetaCharacter meta`, etc.), and CORE helpers
   that all scenes share (`copy_pgm_str`, `draw_engraved_portrait`,
   `draw_menu_pgm`, etc.).
9. **`tools/scene_audit/`** — extend SCENE_RULES to also classify by
   the `.scene.*` section attribute (read from `avr-objdump -h`); add
   a "should this be paged" warning on any function in CORE that the
   audit thinks could move to a scene.
10. **`tools/efficiency_lint/`** — already has
    `scene-root-without-attribute`. Add a sibling `cross-bank-call`
    rule once the section attributes are in place: any `.scene.X`
    function calling a `.scene.Y` function (Y ≠ X) is an error.

## Boot sequence

1. AVR resets, jumps to Caterina at 0x7000.
2. Caterina runs (USB sniff for upload, then jump to 0x0000).
3. Application starts at 0x0000:
   a. `__do_copy_data` — initializes `.data` from PROGMEM.
   b. `__do_clear_bss` — zeroes `.bss`.
   c. C++ ctors (audio descriptor tables, etc.).
   d. `main()` → `display::init()` → `audio::init()` →
      `storage::init()` → `game::init()` → `scene::switch_to(SCENE_TITLE)`.
4. SCENE_TITLE is boot-resident: `switch_to(SCENE_TITLE)` short-circuits
   if `current_id == SCENE_TITLE`. We seed `current_id = SCENE_TITLE`
   in static init, and the title's `on_enter` fires once on first
   call (TITLE has nothing to init beyond vtable wiring).
5. First real swap: TITLE → MAIN_MENU when the user presses A.

The boot-resident scene is **TITLE**, not MAIN_MENU. Reasoning: TITLE
needs to be visible within ~50 ms of power-on (the LZ77 logo decode is
already on the critical path). If MAIN_MENU were boot-resident, we'd
need to swap to TITLE before the user sees anything, which is
backwards.

This means the linker is configured to emit SCENE_TITLE's
`.scene.title` section at `__scene_bank_start` for the main ELF, and
the cold copies of the *other three* scenes are what land in
data.bin's tail. SCENE_TITLE itself doesn't need a cold copy on FX
(it's always re-pageable from internal flash if we ever leave + come
back, by re-linking the cold copy at build time — same input bytes,
just re-packaged for SPM). Captured: TITLE *does* get a cold copy,
because the user can return to the main menu via "QUIT" → TITLE again
on subsequent power cycles, and a return to TITLE from PLAY would
otherwise be impossible.

## Failure modes & recovery

1. **FX corrupt / missing.** `data_flash::read` returns 0xFFs. `switch_to`
   reads the magic, sees ~0xFFFF instead of 0xCE5C, halts and
   draws an on-screen "FX CORRUPT — REFLASH" message using the
   CORE-resident font. No silent garbage execution.
2. **SPM page-write fails.** Boot library doesn't expose a failure
   signal for SPM (it's hardware-deterministic given the right
   sequence). We trust it. The failure mode if it does fail is a
   mid-flash with stale bytes interleaved with new — the next call
   into the bank will jump to garbage. Detection: post-swap, read
   back the first page and compare to FX. If mismatch → halt with
   error screen.
3. **Power loss mid-swap.** AVR loses power, the bank is in an
   inconsistent state (some pages new, some old, some erased = 0xFF).
   On next boot, the boot-resident scene is SCENE_TITLE, NOT the bank
   contents — Caterina jumps to 0x0000, application's `main()` runs,
   `switch_to(SCENE_TITLE)` reformats the bank with TITLE bytes from
   FX (after we add SCENE_TITLE to the cold copies as captured above).
   Net: power loss mid-swap is recoverable on next boot, with the
   user seeing the title screen.
4. **Flash-cell wear.** ATmega32u4 spec: 10,000 erase cycles per page.
   At 1 swap per minute of play (pessimistic), that's 167 hours of
   play before the first page wears out. Cathy3K-grade wear-leveling
   is out of scope for v1; we ship and observe. Mitigation if it
   becomes a real problem: rotate which physical pages each scene
   lands on across boots (round-robin via EEPROM counter).

## Test plan

1. **Unit:** `tools/scene_audit/` clean run after dispatcher refactor;
   `efficiency_lint` clean run; size gates pass.
2. **SDL:** scene transitions work end-to-end (vtable indirection
   correct; on_enter/on_leave fire). `--no-enemies` mode for
   isolated testing of MAIN_MENU and SCENE_PLAY.
3. **Ardens (emulator):** full game playthrough exercising all four
   scenes; verify no flash corruption (Ardens dumps the application
   section after each swap, compared against expected).
4. **Audio:** silence during swap, no glitches outside swap, no audio
   ISR crashes during the SPM blackout.
5. **Hardware (one cart):** 1000 swap cycles between MAIN_MENU and
   SCENE_PLAY via a debug menu option. Verify no corruption (CRC of
   bank after each swap), no perceptible degradation in game behavior.
6. **Power-loss:** unplug USB mid-swap (10 trials). Verify boot
   recovers to TITLE without bricking.

## Out of scope (v1)

- Wear leveling. Single-page-rotation is enough for v2 if we hit the
  10K-cycle cap.
- Multi-bank. Single bank is fine given the 4-scene topology.
- Compressed scene cold copies. The bottleneck is SPM, not FX read;
  decompression cost would dominate.
- Cross-platform parity beyond SDL stub. iOS port (per
  business-model.md) will not need paging — it has gigabytes of
  flash.
- Cathy3K migration. Orthogonal flash-headroom optimization; can be
  applied independently before or after this work.

## Order of operations

1. Spec + patterns doc (this doc + `.claude/rules-scenes.md`).
2. Commit baseline: de-inline + audit-tooling + this spec.
3. Dispatcher refactor: vtable indirection, four `scene_*.cpp` TUs,
   `-flto` still on. Re-run scene_audit. **This is the
   load-bearing measurement step.** If CORE doesn't drop dramatically
   here, the paging gain isn't real and we adjust the scope.
4. Move scene-local helpers/state/tables from `game.cpp` into the
   correct `scene_*.cpp`. Audit clean on every commit.
5. Linker script + `.scene.*` section attributes.
6. Cold-copy generation: `tools/fxdata/build.py --scene-elfs`.
7. Swap manager: `engine/scene.cpp` + `platform/arduboy/scene_spm.cpp`
   + `platform/sdl/scene_spm.cpp`.
8. Caterina SPM dispatch verification on Ardens, then on hardware.
9. 1000-cycle stress test.
10. Ship.
