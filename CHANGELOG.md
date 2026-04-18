# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.4.0] - 2026-04-18

### Added

- Weapons now visually appear in character hands, anchored per-frame to the body animation.
- Dual-wield support with independent left/right hand equipment, attacks, and weapon leveling.
- Two-hand weapon mode toggled with Alt key, rendering the weapon across both hands.
- AK-47 assault rifle with rifle_shoot animation and top-down N/S perspective icon.
- Controls screen accessible from the main menu showing all keybindings.
- Runtime palette swapping for character customization colors, reducing asset files from 518 to 46.

### Changed

- Pistol renamed to Colt .45 with new pixel art icons and no body attack animation.
- Equipment slots renamed from Main Hand/Off Hand to Right Hand/Left Hand.
- Animation system refactored to separate engine playback from game state mapping.
- Equipment model switched from item copies to inventory index references.

### Removed

- Semi-Auto weapon replaced by AK-47.

### Fixed

- UI click-through from menu screens to gameplay.
- Player continuing to move during attack animations.

## [0.3.0] - 2026-04-11

### Added

- Character customization: pick body, hair, hair color, and facial hair when starting a new save. Sprites are placeholder art from the open-source LPC asset set and will be replaced as the game's own art is produced.

## [0.2.0] - 2026-04-07

### Added

- In-game self-update: checks for new releases on launch, downloads and installs in-place.

### Changed

- Saves now live in `%APPDATA%/PrisonEscapeGame/` and auto-migrate from the old per-build location.

## [0.1.0] - 2026-04-05

:seedling: Initial release.

[unreleased]: https://github.com/alexbeams1898/prison-escape-game-releases/compare/v0.4.0...HEAD
[0.4.0]: https://github.com/alexbeams1898/prison-escape-game-releases/releases/tag/v0.4.0
[0.3.0]: https://github.com/alexbeams1898/prison-escape-game-releases/releases/tag/v0.3.0
[0.2.0]: https://github.com/alexbeams1898/prison-escape-game-releases/releases/tag/v0.2.0
[0.1.0]: https://github.com/alexbeams1898/prison-escape-game-releases/releases/tag/v0.1.0
