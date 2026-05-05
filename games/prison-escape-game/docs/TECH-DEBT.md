# Prison Break Game — Tech Debt

Known structural issues to fix at the root, not paper over. Add to this list any time
something is identified as architecturally wrong.

---

- **TileMapLoader belongs in game/, not engine/.** `engine/src/TileMapLoader.cpp` contains
  room placement, corridor carving, and the full procgen pipeline -- all game-specific logic.
  Engine should only keep `TileMap` (data struct) and `TileMapRenderer` (rendering). Move
  `TileMapLoader` to `game/` and expose only the data structures from engine.

- **WeaponSpriteSystem yOffset assumes colliders do NOT scale with character.** The
  `wielderYOffset` math multiplies `sprite.src_h * wielderScale` but subtracts the raw
  `collider.height` (no scale). This is correct today because colliders are fixed in world
  units regardless of visual scale -- a 1.2x character still has a 32x32 collider. If a
  future feature scales colliders alongside the character's visual scale (giants,
  shrink debuff, etc.), this line will produce a wrong yOffset and the weapon will drift
  from the hand at non-1.0 scales. Revisit `src/systems/WeaponSpriteSystem.cpp`
  `wielderYOffset` calculation if collider scaling is ever introduced.

- **Engine tests depend on prison-escape-game's tile config.** `engines/engine/tests/tilemap_test.cpp`
  loads `config/tilemap.json` and `config/rooms` from the game's directory (its WORKING_DIRECTORY
  in CMake points at `games/prison-escape-game/`). Engine tests should be self-contained; either
  inline a fixture tilemap into the test, or extract a minimal test fixture into
  `engines/engine/tests/fixtures/`. Cross-referenced by the engine/game boundary doctrine.
