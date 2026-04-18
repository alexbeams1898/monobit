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
  from the hand at non-1.0 scales. Revisit `game/src/systems/WeaponSpriteSystem.cpp`
  `wielderYOffset` calculation if collider scaling is ever introduced.
