# Scene-paging boot-recovery bug — investigation notes

**Status:** Bug 1 diagnosed and fix designed. Bug 2 partially understood;
real-hardware vs Ardens behavior diverges. Both deferred to a dedicated
PR.

**Severity:** Real shipping bug present on `master`. Affects every
play session: any reset / power-cycle after entering the wood menu
puts the chip in an unrecoverable state on next boot (white screen on
master; OOB-deref or PC-into-garbage on the major/selva-oscura-hub-world
branch — same root cause, different layout-dependent symptom).

**First observed:** 2026-05-04 during the major/selva-oscura-hub-world
branch, while diagnosing an OOB-deref auto-break in Ardens after the
sequence "title → A → enter name → A → wood menu → reset".

## Reliable repro

1. `make ardens-fresh` (wipes EEPROM + save sector).
2. Title screen appears.
3. Press A.
4. Enter name (or skip past name entry if a name already exists).
5. Reach the wood menu (selva oscura).
6. Tools → Simulation → Reset (or any reset that preserves flash —
   real hardware power-off-on does the same thing).
7. Boot 2 starts. **White screen / OOB-deref / black screen,
   depending on layout.**

A simpler repro that also fires Bug 1: `make ardens-fresh`, title → A,
reset during the Beatrice cover animation (mid-SPM-swap).

## Bug 1 — root cause

The scene-paging architecture stores 4 banks (TITLE, MAIN_MENU, GATE,
PLAY) at the same VMA `__scene_bank_start = 0x4900` via OVERLAY linker
sections. Only one bank's bytes are physically resident in flash at
any moment; the others live as cold copies on FX. Swapping banks
requires SPM-rewriting the resident region with the new bank's bytes
(via the `kp_boot_32u4` bootloader trampoline on hardware, or an
in-app SPM stub on Ardens via `ARDENS_SPM=1`).

**Flash is non-volatile.** Reset / power-cycle preserves the resident
bank's bytes. After the user has reached MAIN_MENU, the bank holds
MAIN_MENU's bytes. After reset, the bank STILL holds MAIN_MENU's
bytes — but boot's `game::init()` calls `scene::set_active(SCENE_TITLE)`
expecting TITLE's bytes to be there.

`set_active` had a "boot-resident scene optimization" (engine/scene.cpp
prior to this fix) that **skipped page_in** when `current_id == ID_COUNT
&& id == ID_TITLE` (the first call on fresh boot, requesting TITLE).
The intent: avoid playing Beatrice's exit animation as the first
thing the user sees on cold boot. Side effect: the recovery page_in
that would have restored TITLE bytes from FX never happened.

Result: boot then dispatches `update_title_scene` via the vtable. The
vtable's `update` fnptr resolves to `0x4900` (bank base). Execution
lands on **MAIN_MENU code** instead of TITLE code. Wild execution
follows. Symptom depends on what exact bytes happen to be where:

- Bytes that decode as a valid `lds 0xXXXX` where XXXX > RAMEND →
  Ardens reports OOB-deref auto-break.
- Bytes that produce a corrupt return address → OOB-PC auto-break OR
  PC into uninitialized flash region → garbage execution → eventual
  white screen.
- All other patterns → white/black screen with the chip stuck
  somewhere.

The original `docs/scene-paging.md` "Power loss mid-swap" section
(line 638-643) anticipated this exact case and described the recovery
("Caterina jumps to 0x0000, application's main() runs,
switch_to(SCENE_TITLE) reformats the bank with TITLE bytes from FX")
— but the optimization in set_active **prevented that reformat from
happening**. The recovery was never actually wired up.

## Bug 1 — fix

**Always page_in on boot's first set_active call**, regardless of
which scene is requested. Skip cover/uncover (the original optimization's
narrative intent — no Beatrice exit animation on fresh boot — IS
valid), but **don't** skip the page_in.

```cpp
// engine/scene.cpp set_active:
const bool skip_cover = (current_id == ID_COUNT && id == ID_TITLE);
if (!skip_cover) {
  platform_scene_loading_cover(current_id, id);
}

#ifdef SCENE_PAGING_ENABLED
// Always page in, including on the boot-skip-cover case.
platform_scene_page_in(id);
#endif

if (!skip_cover) {
  platform_scene_loading_uncover(current_id, id);
}
```

Cost: ~400 ms of SPM time on cold boot (the first set_active(TITLE)
runs the page-write loop). User-visible only as a brief delay
between "main starts" and "title screen appears". A loading-card
animation could mask it later if needed; for now, the brief black
screen at boot is acceptable (and has narrative justification —
the chip "wakes" before the player sees Hell's gates).

## Bug 2 — interrupt interaction with Ardens SPM

When Bug 1's fix is applied AND `cli()` is held during the SPM
loop (the original code's pattern), Ardens' SPM emulation
**produces zeros instead of the loaded buffer's bytes**. After
removing `cli()` from the loop, SPM works correctly on Ardens.

But removing `cli()` introduces a different freeze: somewhere in the
title-A → MAIN_MENU swap path, execution wanders into the (mid-write)
bank and lands on an undecodable instruction.

### What we know

- Ardens emulates SPM via a peripheral-queue scheduler. The page-write
  completion fires after a simulated cycle delay (`SPM_CYCLES =
  16000000 / 250 = 4 ms` worth of cycles).
- With `cli()` held, the wait loop in `app_spm_trampoline_entry`
  spins waiting for SPMEN to clear — but in Ardens, SPMEN seems to
  not clear correctly without interrupt-driven scheduler ticks.
- Without `cli()`, the audio ISR (Timer4 OVF, 16 kHz) fires periodically
  and advances the simulator's cycle counter. SPM completes correctly.

### What we don't know

- Whether real `kp_boot_32u4` hardware exhibits the same `cli`
  sensitivity. Probably NOT — real silicon's SPMEN bit clears on a
  hardware-fixed schedule (~3.5 ms erase, ~4.5 ms write per page,
  per AVR datasheet) regardless of interrupt state. The behavior
  is Ardens-specific.
- What causes the freeze with `cli()` removed. The audio ISR is
  CORE-resident and never calls into the bank, so it shouldn't be
  the trigger. May be a different ISR (Timer1 / Timer4_COMPA / etc.)
  or some other code path firing during the page-write window.

### Hypothesis (unverified)

Ardens' `update_spm` function relies on the peripheral queue advancing,
which advances when the CPU clock advances. In a tight `cli`-protected
busy-wait, the CPU clock DOES advance — but maybe Ardens' peripheral
queue only fires on instruction-boundary events that are gated by
interrupt-enabled state. Need to read Ardens source more carefully
to confirm.

The fix on Ardens should be: keep `cli()` only around the actual SPM
instruction (the 1-cycle `spm` opcode), not around the wait loops or
the buffer-fill loop. The wait loop should run with interrupts
enabled so the scheduler can advance.

The fix on real hardware should be: keep `cli()` around the entire
swap loop (current code's pattern is correct for hardware — interrupts
disabled during page-erase + page-write keeps the audio ISR from
trying to read FX during the SPM-busy window, which would collide
with SPM).

### Open questions

- Does Bug 2 reproduce on `master`? `master` doesn't run page_in on
  boot (Bug 1's optimization skip), so the SPM load only fires on
  the title-A → MAIN_MENU path. That path has worked historically.
  So Bug 2 may be specific to the boot's force-page-in case.
  Untested.

- Real-hardware testing: required before shipping the fix. The
  `kp_boot_32u4` bootloader's SPM trampoline lives in NRWW (boot
  section); SPM from BLS to RWW (where the bank lives) doesn't halt
  the CPU. Audio ISR keeps running. This is fundamentally different
  from Ardens' single-threaded simulation.

## Path forward

1. **Land Bug 1's fix on a dedicated PR (chore/scene-paging-boot-recovery).**
   The "always page_in on boot" change is correct and necessary,
   independent of Bug 2.
2. **Verify on real hardware.** With kp_boot_32u4 installed on a
   physical Arduboy, run the same repro: title → wood menu → power
   off → power on. Should land cleanly on title.
3. **If real hardware works, leave Bug 2 as Ardens-only quirk.**
   Document it as "Ardens behaves differently from hardware here;
   the hardware behavior is correct."
4. **If real hardware also breaks**, dig deeper. The most likely
   culprit there would be a different ISR (USB / Timer1 / etc.) or
   a brown-out detector firing during the long SPM window.

## Investigation cost

Approximately 5 hours of session time was spent on this bug. The
length is itself diagnostic-worthy: the scene-paging architecture is
fragile in a way that doesn't surface until reset behavior matters.
The codebase has accumulated several layout-sensitive workarounds
(`__attribute__((section(".hightext")))`, manual noinline,
`-mcall-prologues` historically, etc.) that compound the difficulty
of adding diagnostic instrumentation — every breadcrumb shifts
addresses, which shifts the bug's manifestation, making it
non-repeatable across diagnostic builds.

Followups (separate work):
- CI scenario test: headless Ardens runs scripted "title → A →
  reset" and asserts clean boot. Catches Bug 1 instantly going
  forward.
- Boot-time bank-state checksum: read first 4 bytes of bank,
  compare to expected TITLE magic, force page_in if mismatched.
  A defensive check in addition to the unconditional page_in.
- Real-hardware verification flow.
