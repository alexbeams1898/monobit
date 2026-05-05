# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

Per `docs/platform-parity.md`, every change MUST behave identically on
Arduboy and SDL. Categories below describe player-perceived behavior;
platform-mechanism differences (Arduboy SPM vs SDL no-op paging, AVR
EEPROM vs SDL save.bin, etc.) are implementation details and not
changelog-worthy unless they leak into player-perceived behavior.

## [Unreleased]

### Added

- Vestigia save system: 8 manual save slots backed by FX flash, with a popup confirm flow (LOAD / SAVE / OVERWRITE / CANCEL) on the VESTIGIA wood menu.
- Autosave: every wood arrival writes the Pilgrim's current state to slot 0 (the AUTOSAVE row); player can load it from the slot list to return to that state.
- Vestigia stress test (`tests/vestigia_smoke.cpp`): exercises maxed-out fields, all 8 slots populated, boundary byte patterns, persistence across restart, 100-write ping-pong churn, cross-slot integrity.

### Changed

- Vestigia slot writes are now chunked at 32 B per page-program call (was 256 B per slot, on the stack), eliminating the stack-overflow that corrupted `audio_phase_inc` and the framebuffer tail when the Pilgrim pressed A on a slot row.
- VESTIGIA slot list reads class+burden from a cached metadata array per slot; previously hardcoded to "UNBURDENED" for slot 0 and "(empty)" for slots 1..7 regardless of FX flash state.
- Slot 0 row label changed from "UNBURDENED" to "AUTOSAVE" to reflect its design role.

### Fixed

- Pre-commit parity gate now actually rebuilds the SDL target on every commit. Previously `$(SDL_EXE)` only depended on `build.ninja`, so Make would skip the cmake invocation when sources changed (since CMakeLists.txt hadn't changed) and reuse a stale binary. Result: silent platform-parity drift across multiple commits while CI reported success.

### Lore

- "Hell does not let the Pilgrim hoard" — new section in `docs/lore.md` sketching a hybrid soft-cap design (overflow loss at the wood + scaling investment cost tied to vessel size). Specific tuning is TBD; the principle is that sangue cannot be saved indefinitely.
- Design-tree fleshout: `docs/design/` is now the authoritative design surface. `setting.md` and `story.md` lock the cosmology, the Vagrant, the Guide, Beatrice (stir-crazy / rabid descent on REFUSAL/PURITY), the four endings, and the five reveals (R1-R5). `classes.md`, `economy.md`, `fallback.md`, `inventory.md`, `companions.md`, `dialogue.md`, `pc-vs-npc.md`, `ux.md`, `character-creation.md` are filled at structural level. Working title locked: *Selva Oscura*. Death-card spec locked: *NOT YET / THOU DOST NOT BELONG*. *Senza Forma* named as the cosmological rule that gates Hell's resolution. *Disgorgement* named as the wallet-zero-on-segment-boundary mechanic. Footer-hint convention locked in `ux.md`.
- Legacy lore docs `docs/lore.md` and `docs/game-design.md` removed; their load-bearing content has been absorbed into the design tree (Disgorgement → `economy.md`; footer hints → `ux.md`). `docs/dante-cliffs.md` retained as source-material reference and lightly updated. Stale cross-references across the codebase (issue templates, PR template, code comments, `.claude/rules-design.md`) repointed at `docs/design/`.

[unreleased]: https://github.com/alexbeams1898/mono/compare/HEAD...HEAD
