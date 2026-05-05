// Vestigia — the player-facing save system. Each vestigium is a frozen
// snapshot of the Pilgrim at a moment in his cycle, kept on FX flash
// in a dedicated 8 KB save region (see data_flash.h save_*). The
// player captures, loads, and overwrites vestigia from the wood; the
// game also writes a hidden autosave (the "ledger") whenever the
// Pilgrim arrives at the wood.
//
// Lore: docs/design/ "The ledger and the vestigia". The ledger is
// Hell's bookkeeping (mundane, invisible); vestigia are the Pilgrim's
// transgressive act of stepping back into his own past states.
//
// On-disk: docs/save-format.md (v1 layout). The save region is two
// 4 KB sectors used in ping-pong: every write erases-and-writes the
// "other" sector, then the higher generation counter on the new
// sector promotes it to current. Power loss anywhere in the write
// cycle leaves the previous-known-good sector intact.
//
// Naming-collision note: `MetaCharacter::vestige` (in storage.h) is an
// unrelated field that names the *class* (UNBURDENED, PENITENT, etc.).
// In code, `meta.vestige` is always the class; vestigia (this file)
// are saved traces. See docs/design/ "Naming note (collision)".

#pragma once

#include "storage.h"  // MetaCharacter — what each slot serializes
#include "types.h"

namespace vestigia {

// ---- Layout constants (frozen for v1) -------------------------------

// Total slots: slot 0 is the unburdened vestige (permanent, written
// once on first boot, never overwritten); slots 1..7 are user-managed.
// Sized to leave headroom against the "max 10" UI ceiling without
// burning more sector space than necessary.
constexpr u8 SLOT_COUNT = 8;

// Per-slot byte budget. One chip page (256 B) so each slot writes as
// exactly one page-program. MetaCharacter today is 26 B, so we have
// ~225 B of growth headroom inside one slot before we'd need to bump
// the layout version.
constexpr u16 SLOT_SIZE = 256;

// Format version. Bump when the on-disk schema changes; the loader
// has a migrator per prior version that reads old, writes new.
constexpr u8 FORMAT_VERSION = 1;

// Slot 0 is always the unburdened vestige and is written exactly once
// on the first wood-arrival of a fresh save. The wood UI never
// presents slot 0 as an overwrite target; the only writer is the
// internal bootstrap. See lore: "Slot 0 — the unburdened vestigium".
constexpr u8 SLOT_UNBURDENED = 0;

// ---- Public API -----------------------------------------------------

// Status returned by load operations. Lets the wood UI distinguish
// "this slot is empty" from "this slot is corrupt" from "the whole
// save region is unreadable" — different player-facing messages for
// each.
enum class Status : u8 {
  OK,              // load succeeded; out parameter is filled
  EMPTY,           // slot is unwritten (post-erase) — not an error
  CORRUPT_SLOT,    // slot magic ok but checksum mismatched — single slot lost
  CORRUPT_SECTOR,  // both ping-pong sectors invalid — whole save region lost
  WRITE_FAILED,    // save attempt failed verify (worn cell / hardware fault)
};

// Initialize the vestigia subsystem. Reads both ping-pong sectors,
// picks the higher-generation valid one as current, falls back to
// blank-state if neither is valid. On a brand-new save region (chip
// erased / first boot), this notices the all-0xFF state and primes
// the ledger by writing slot 0 with the unburdened MetaCharacter.
//
// Call once at boot, after data_flash is ready. Idempotent: subsequent
// calls re-read state but don't touch the chip.
void init();

// Read a slot's MetaCharacter into `out`. Returns OK on success;
// EMPTY if the slot is unwritten; CORRUPT_SLOT if the slot has bad
// magic or bad checksum (out is left untouched).
Status read_slot(u8 slot, storage::MetaCharacter& out);

// Capture the current MetaCharacter into a slot. Refuses to write
// slot 0 (returns WRITE_FAILED) — the unburdened slot is permanent
// and its only writer is the internal bootstrap. Refuses if `slot`
// is past SLOT_COUNT. On success the new state is committed to the
// non-current sector and that sector becomes current; on failure
// (verify mismatch) the previous current sector is unaffected.
//
// No erase API: the player can only overwrite a vestigium with a new
// capture, never remove one. Lore: a vestigium is the Pilgrim's
// deliberate transgression — once captured, it persists. Hell does
// not let you forget on demand. Simplifies the protocol (no nullable
// arg in commit_slot_change) and the UI (sub-menu is LOAD /
// OVERWRITE / CANCEL, no third axis).
Status write_slot(u8 slot, const storage::MetaCharacter& m);

// Autosave. Captures `m` into slot 0 (the AUTOSAVE row). Wood-arrival
// code calls this on every entry to the wood; nothing else should.
// Bypasses write_slot's slot-0 gate (which exists to prevent the
// player from manually overwriting AUTOSAVE from the slot-list UI).
// Same ping-pong commit semantics as write_slot.
Status write_autosave(const storage::MetaCharacter& m);

// True if slot `s` currently holds a captured vestigium (last read
// returned OK). Cheap accessor for the wood UI's slot-list rendering —
// doesn't re-read the chip; uses cached state from the most recent
// init()/write_slot()/write_autosave() call.
bool slot_occupied(u8 s);

// Cached vestige (class) and burden (level within class) for slot `s`.
// Valid only when slot_occupied(s) is true; undefined otherwise. The
// slot-list UI uses these to render the row's class+burden icon and
// label without re-reading FX flash on every frame.
u8 slot_vestige(u8 s);
u8 slot_burden(u8 s);

}  // namespace vestigia
