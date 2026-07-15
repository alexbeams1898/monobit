# Wayworn Hush — Inventory

> **Status:** BUILT. `Inventory.h/.cpp` (data model + ops + loader), a Satchel
> pause-page tab, notebook/watch key-items with carried effects, item-gating,
> and the "New" find badge all ship. `tests/inventory_test.cpp` covers the ops.
>
> **Locked:** three categories (Practical / Keepsake / KeyItem); the notebook's
> recording capability **gates on carrying it** (`has(satchel,"notebook")` is a
> real gate, not flavor); the dated record is a **separate structure keyed by
> ownership**, not stored inside the ItemInstance.
>
> **Since the proposal:** the per-copy `quality` field was DROPPED — this game
> has no quality axis (unneeded complexity for a secondary system). Rarity is
> per-TYPE only. A `Practical` item is defined solely by its id + quantity.

## What it has to serve (from DESIGN.md / GAME-SYSTEMS.md)

- **Light, no weight torture.** "Inventory management is light." No encumbrance,
  no slot-Tetris. A quiet satchel, not a Souls bag.
- **Found by attention.** Items come from observing/gathering the world — the
  same act that grows the self. Picking a thing up is a reward for noticing.
- **Three roles the fiction already names:**
  - *Practical* — cooking ingredients, herbs, crafting materials (feed the
    gather→craft→eat loops; consumed).
  - *Collection* — trinkets, notable rocks/feathers/seedpods; kept, not spent;
    an attention-reward.
  - *Key items* — the **notebook**, the **watch**, lantern/cloak (Metroidvania
    gating "dressed as survival prep"), skill-unlockers. Unique, never consumed,
    often *do something by being carried*.
- **Item-gating is load-bearing.** "Certain items are required to progress." So
  `has(item)` is a first-class query other systems ask.
- **Materials carry ONE axis:** *Rarity* (per-type, 1..5). There is no per-copy
  quality — that idea was dropped as unneeded complexity. A material is its id +
  a quantity; two copies of `wild_thyme` are identical.

## The model — two layers (borrowed skeleton, trimmed hard)

Same proven split prison-escape uses, without its ~90-field weapon kitchen-sink.

### `ItemDef` — the immutable blueprint (authored, one JSON per item)

Read-only after load. Keyed by a short **string id** (not a file path — a stable
id survives moving files). Deliberately small; category-specific data goes in a
sub-struct, not flattened into every item.

```
struct ItemDef {
    std::string id;             // "river_stone", "notebook", "wild_thyme"
    std::string name;           // display name
    std::string description;    // flavor / what it is
    std::string icon;           // sprite path (placeholder ok)
    ItemCategory category;      // Practical | Keepsake | KeyItem  (see below)
    int rarity = 1;             // per-TYPE, 1..5 -- SAME scale as reading difficulty
                                // (reading_color::rarityWord/rarityColor); 0 = no rarity
    bool stackable = false;     // Practical stacks; Keepsake/KeyItem do not
    int  max_stack = 1;
}
```

`ItemCategory` — three roles, matching the fiction (not prison-escape's seven):
- `Practical` — ingredients/materials. Stackable. Consumed by cook/craft/eat.
- `Keepsake` — collection/attention-reward. Unique-ish, kept, no mechanical use
  (yet — the rarest keepsakes become "pieces of the self," GAME-SYSTEMS §5; that
  hook lands later).
- `KeyItem` — notebook, watch, gating gear, skill-unlockers. Unique, never
  consumed, queried by `has()`, and may grant a **carried effect** (below).

Rarity **reuses the reading rarity vocabulary** (`ReadingColor`) so an item's
rarity reads in the same visual language as a thought's — one rarity system, not
two.

### `ItemInstance` — a concrete copy in the satchel

```
struct ItemInstance {
    std::string id;         // -> ItemDef
    int   quantity = 1;     // for stackables
    bool  is_new = true;    // "newly found" marker for the UI (mirrors reading is_new)
}
```

`is_new` mirrors the observation system's "New": the satchel badges a fresh find
until the player views the Satchel tab, then `markAllSeen()` clears it on leave.

### Player state — one component, a plain vector

```
struct Satchel { std::vector<ItemInstance> items; };
```

No `max_slots`, no equipment-index scheme (no equip slots in this game). "Light
management" = an unbounded, unordered satchel. If a soft cap is ever wanted it's
one int later; default is no cap.

### Registry — the loaded defs

```
struct ItemRegistry { std::unordered_map<std::string, ItemDef> defs; };
```

Loaded once from `config/items/*.json` (one file per item, id = filename stem or
an explicit `id` field). Lives in the registry-context `GameState` like the other
authored data (observations, growth).

## Operations — free functions, `namespace inventory`

Plain functions over the structs (matches the engine's helper-namespace idiom;
pure, unit-testable without GL):

```
void  add(Satchel&, const ItemRegistry&, ItemInstance);   // stackables merge
bool  remove(Satchel&, const std::string& id, int qty=1); // false if not enough
int   count(const Satchel&, const std::string& id);       // total across stacks
bool  has(const Satchel&, const std::string& id);         // count > 0  (gating)
```

`has()` is a named function (not `count>=1`) because **item-gating reads it
everywhere** — readability at the call site matters more than saving a helper.

## The Notebook + Watch as the first two KeyItems

This is why we're building inventory now: the notebook is a **key item**, and its
job (owning the dated observation+thought record) is a **carried effect**.

- **Carried effect.** A `KeyItem` can grant a capability the game checks with
  `has(satchel, "notebook")`. The notebook's effect: *the pilgrim writes things
  down* — it's what turns fired thoughts into dated notebook entries. Without it,
  thoughts still happen (the head bubble, the box) but aren't recorded.
- **The watch's effect:** `has(satchel, "watch")` → the notebook shows the
  **dateline** (the `worldclock::stamp()` seam we already built). No watch = the
  entry has no time. This is exactly the "display is a caller decision" hook
  WorldClock.h already anticipates.
- So the dated-record model becomes: **the Notebook is a key item that owns the
  record; the Watch is a key item that unlocks the *time* on each entry.** The
  inventory is the substrate; the notebook UI is a *view onto the notebook item's
  data*.

**Locked:** the record lives in a **separate `NotebookRecord` in GameState**,
NOT inside the notebook `ItemInstance`. The item is a small handle/gate; the
record grows without bloating the item schema, and it serializes cleanly on its
own. `has(satchel,"notebook")` gates writes into the record; `has(satchel,
"watch")` gates the dateline on each entry.

```
GameState:
  Satchel        satchel;   // holds the notebook item (+ watch, later)
  ItemRegistry   items;     // loaded defs
  NotebookRecord record;    // dated observations + thoughts (its own structure)
```

## What this deliberately does NOT include (deferred, with homes)

- **Crafting / recipes / mastery chains** (GAME-SYSTEMS §5) — future; `Practical`
  items + `remove()`/`count()` are the substrate it'll build on.
- **Equip slots** — none in this game (no weapons/armor). Omitted entirely.
- **A pickup/gather world-interaction** — how an item physically enters the
  satchel (walk-over? an observe→gather action?) is a small follow-on; the
  observation system's action menu is the likely entry point.

## Where it lives

- `include/Inventory.h` + `src/Inventory.cpp` — structs + ops + JSON load.
- `config/items/*.json` — authored items (notebook, watch, a few placeholders).
- `GameState` gains `Satchel satchel;` + `ItemRegistry items;`.
- The satchel view is a pause-page tab or sub-view (peer of Self / Noticed).
- `tests/inventory_test.cpp` — add/remove/count/has/stack-merge (pure).

## First slice (once the model is agreed)

1. `Inventory.h/.cpp` structs + ops + loader + tests. No UI yet.
2. Author `notebook` + `watch` + 2-3 placeholder items; player starts with the
   notebook.
3. Wire `has(satchel,"notebook")` as the gate on recording notebook entries, and
   `has(satchel,"watch")` as the gate on the dateline.
4. A satchel view in the pause page.
5. (Later) the dated notebook-record sub-design; gather/craft.
