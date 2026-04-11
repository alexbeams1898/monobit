# Prison Break Game — Tech Debt

Known structural issues to fix at the root, not paper over. Add to this list any time
something is identified as architecturally wrong.

---

- **TileMapLoader belongs in game/, not engine/.** `engine/src/TileMapLoader.cpp` contains
  room placement, corridor carving, and the full procgen pipeline -- all game-specific logic.
  Engine should only keep `TileMap` (data struct) and `TileMapRenderer` (rendering). Move
  `TileMapLoader` to `game/` and expose only the data structures from engine.
