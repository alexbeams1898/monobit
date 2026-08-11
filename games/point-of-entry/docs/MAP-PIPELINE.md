# Point of Entry — map pipeline

Authored space is built in **LDtk** against one project file the game reads
directly. Levels are areas; entities are the world's objects; the tileset's
enum tags are collision. The homemade layer of the workflow is the ART —
tiles and sprites drawn in-house and fed to LDtk as tilesets — not the
editor. (Reference implementation of the pattern: wayworn-hush's pipeline.)

## The contract (what the importer reads)

**One project:** `assets/maps/world.ldtk`. Dev builds read it straight from the
source tree — save in LDtk, relaunch the game, it's live. `scripts/check_areas.py`
lints the warp contract.

**Grid:** author at **16px** — the art's native GBC-register scale. The
importer doubles everything to the 32px world; the ×2 lives in one place in
code and nowhere in the project.

**Layers per level (exact names):**

- `Ground` — a Tiles layer painted from the project tileset. The first tile
  in a cell is the base; stacked tiles become the decoration layer, drawn in
  paint order under characters. A cell walks only if every tile in it does.
  Unpainted cells are void: solid, rendered as the clear colour.
- `Entities` — everything placed. Every entity becomes a typed object: the
  PascalCase identifier lowers to the builder key (`Warp` → `warp`,
  `PlayerStart` → `player_start`), and entity fields arrive verbatim as the
  object's props. Keep entity pivots at the default top-left.

**Collision:** a tileset enum with the value **`Solid`**; tag the blocking
cells in the tileset editor. No painted collision layer — walkability
travels with the art.

**Entity definitions:**

| Entity | Fields | Meaning |
|---|---|---|
| `Warp` | `id`, `target` (strings), `facing` (Facing enum) — all required | A passage you walk THROUGH -- doorway, staircase, hole; the tiles under it are the look. `target` names the WARP it arrives at — never a level; the level falls out of the boot-time index. Size the entity to the doorway. `facing` = the cardinal you step out along when you ARRIVE here; the warp fires when your feet cross the opposite edge moving outward — entering the strip, backing out, or crossing sideways all do nothing. |
| `PlayerStart` | — | THE start: exactly one in the whole project, and a new game wakes on it. Every other level is entered through its warps. Without one the game falls back to a bare generated floor. |
| `RestSpot` | — | The staging area. |
| `Prop` | `size` (float), `solid` (bool), `sprite` (string, optional) | A placed thing; a box until it names sprite art. |
| `DigSite` | `depth` (int, default 0), `sprite` (string, optional), `trickle` (string, optional), `interval` (float, default 8) | The way down. In range it offers Descend; interacting generates the dungeon floor at `depth`. `trickle` names a creature the site leaks (`ant` — bestiary names, not paths) — one every `interval` seconds, accumulating until the room is left. A room with a dig site counts as an extermination zone: the weapon fires there and in dug floors, nowhere else. |

Adding a new kind of thing = a new entity definition here + a registered
builder in code (`area::registerBuilder`). The importer never changes.

**Tileset:** `assets/tilesets/placeholder.png` exists to start with; replace
with real 16px art as it lands. (Its cells are flat colour, so at a 16px
grid it reads as quarters — left ones floor, right ones wall; tag ALL the
wall quarters `Solid`.) The `.ldtk` stores the image path relative to
itself, so from `assets/maps/` point it at `../tilesets/placeholder.png` —
the importer resolves it either way.

## Project setup checklist (once, in LDtk)

1. New project → save as `assets/maps/world.ldtk`. Default grid 16.
2. Add the tileset (`../tilesets/placeholder.png`, 16px cells).
3. Create an enum `Surface` (or any name) with value `Solid`; in the tileset
   editor, tag the wall cell(s) with `Solid`.
4. Layers: `Entities` (entity layer), `Ground` (tiles layer, the tileset).
5. Entities: define the five in the table above (Warp with `id`/`target`
   strings and the `Facing` enum field; Prop with `size`/`solid`/`sprite`).
6. Make a level, paint a room, drop a `PlayerStart` — F1 → Levels in-game
   lists it immediately.

## In-game verification

F1 (dev panel) → **Levels** enters any level at its start; **Warps** lands
on any doormat — the exact state walking through its counterpart produces.
Walking out through a warp fades through black and arrives at the warp its
`target` names, standing on its facing side; turning straight around walks
you back the way you came.
