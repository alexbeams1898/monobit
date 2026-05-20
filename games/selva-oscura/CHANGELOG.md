# Changelog

All notable changes to Selva Oscura will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.1.0] - 2026-05-20

### Added

- Inner Wood hub area with heightmap-driven terrain (gentle 11deg colle ascending to a plateau, surrounded by forest, no visible map edges).
- Procedural ground variation with dark humus on flat canopy floor, iron-red sub-soil exposed on slopes, and two-octave value noise mottling at all distances.
- Physically-grounded atmosphere via Rayleigh + Mie ray-marched scattering shared between sky dome and in-scene fog; horizon fades seamlessly into the wood-floor color past the heightmap extent.
- Per-foot terrain sampling so the player and enemies plant their feet on the heightmap surface instead of a flat ground plane.
- Authored tree placement along the colle approach and back-side aisles, with background scatter biased around the colle and per-variant root-depth tuning to eliminate floating bases.
- Eight-direction locomotion with WASD + strafing, facing-to-move-direction, weighted turn rate lerped by angle delta.
- Root-motion gait clips (idle, walk, run, walk-to-run, run-to-stop, 180-degree reversals) with per-clip blend-in durations and translation-source declarations.
- Combat system with weapon classes (sword, buckler, unarmed), weapon instances (longsword, iron buckler), attack chains, on-hand vs off-hand semantic binding, stance switching, and per-clip cancel/chain windows resolved automatically from animation joint motion.
- Enemy AI with perception (vision cone, awareness ladder, distance-based combat retention), data-driven behavior tree (Idle / Alerted / Combat branches), per-archetype action selection, and the Limbo Shade enemy archetype.
- Lock-on Phase A with target acquisition, target-relative camera, and locked locomotion.
- Second-death system with death clip, second-death card, respawn at spawn point, and layered death audio (dark bed plus peak-aligned verdict layers).
- Audio foundation (selva::audio module, config/audio.json sound registry, Chapelflames music track with low-pass duck on death).
- F1 tuning panel for runtime adjustment of locomotion, combat, AI, camera, and audio parameters; saves to config/tunables.json.
- Combat-debug log file capturing per-frame combat, perception, and animation diagnostics on demand.
- Inner-Wood design canon (wood.md, creatures.md, crafting.md, ai.md, combat.md, bestiary.md, setting.md updates) plus a version-bump doctrine in DEV_PILLARS.md.
- Public release repo wired into the release pipeline, CREDITS.md for third-party attributions, plus changelog and test tooling for Selva.

### Changed

- Player and enemy locomotion now share core systems (movement-locked gating during one-shots, root-motion vs velocity translation sources, hip-delta apply) so the same animation behavior holds on both sides.
- Enemy combat retention now uses a distance-based leash with hysteresis instead of vision-based timers, so enemies stay engaged as long as the player is within range regardless of line of sight.

[unreleased]: https://github.com/alexbeams1898/selva-oscura-releases/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/alexbeams1898/selva-oscura-releases/releases/tag/v0.1.0
