# Archived in-tree

This directory holds the original `mono` repo's 1-bit Arduboy engine and
its accompanying RPG (the RPG itself was moved to `games/rpg-arduboy/`).
Imported on 2026-05-05.

## Status

**Read-only reference.** Not part of monobit's CMake build. Do not extend
or modify code here. Bug fixes, feature work, and design changes go to the
main `engines/engine/`.

The narrative IP (Dantean cosmology, the Vagrant, Beatrice, the Guide, the
nine circles) lives on as **Selva Oscura** at `games/selva-oscura/`, a 3D
soulslike on the main engine. Design canon was copied during the import.

## Long-term plan

Features and patterns proven here -- 1-bit rendering, severe RAM/flash
budgets, scene paging, fx-flash for static data, the audio synth -- are
the seed of an eventual 1-bit / low-spec mode in `engines/engine/`. When
that merge happens, this directory deletes.

## Building

The original Makefile and SDL CMake project are preserved (`Makefile`,
`CMakeLists.txt` in this directory). They were written for the `mono` repo
and reference paths relative to *that* repo's root. Building from here may
need light path adjustment.

## See also

- `README.md` in this directory -- the original mono README (preserved).
- `CHANGELOG.md` -- the mono changelog (preserved).
- `docs/` -- mono's engine / platform / known-bugs docs (preserved).
- The historical `mono` repo on GitHub (archived) for the full git history.
