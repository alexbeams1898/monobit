#pragma once

#include "Inventory.h"
#include "LdtkImport.h"
#include "Loot.h"

#include <string>
#include <vector>

#include <entt/entt.hpp>

class EntityManager;

// Items lying in the world. Unlike an observation (marked by a warm glimmer), an item IS
// its own cue: its icon renders on the floor as a Y-sorted world sprite, blending into the
// scene the way a real object would. Interacting takes it directly (fast looting) -- the
// interactable is actionable-only (no reading). A gather node (a harvest spot backed by a
// loot table) renders a configurable stand-in sprite until node art is authored. See
// docs/design/GAME-SYSTEMS.md.
namespace world_items
{

// Feel/placement for a world item's floor sprite (config over constants). Tunable; edit
// config/world_items.json + relaunch (dev reads assets from source).
struct Config
{
    float scale = 1.0f; // draw scale of the item icon on the floor
    float box = 24.0f;  // interaction box size (world px; the reach zone)
    // A gather node has no single item icon (it's a table); this stands in until node art
    // is authored per spot.
    std::string gather_sprite = "assets/sprites/items/wild_thyme.png";
    int gather_size = 32; // gather stand-in cell size (px)
    // The in-reach cue: a lit rim outline on the ACTIVE item (the InteractionSystem's
    // resolved target). Distinct from the observation glimmer -- the item's own edge lights,
    // no floating glow. Driven by Interactable.active in updateOutlines.
    float outline_r = 1.0f;
    float outline_g = 0.95f;
    float outline_b = 0.6f;
    float outline_width = 1.5f; // rim thickness in source texels
    float outline_alpha = 1.0f;
};

// Load the feel from config/world_items.json (silent no-op -> defaults if missing).
void load(Config& cfg, const std::string& path);

// Spawn each world pickup as a floor sprite + an actionable-only interactable: a static drop
// shows its item's `icon` and picks up that item; a gather node shows the stand-in sprite
// and rolls its loot table. Y-sorted by its base so the player draws in front/behind by
// position. An unknown item/table id logs + is skipped.
void spawn(EntityManager& em, const std::vector<ldtk::PickupPlacement>& pickups,
           const inventory::Registry& items, const loot::Registry& loot, const Config& cfg);

// Per-frame: put a rim Outline on each actionable item whose Interactable is the active
// target (the InteractionSystem resolved it -- in reach or hovered), and remove it from
// those no longer active. The item's own edge lights as the "you can take this" cue. Run
// after the InteractionSystem sets `active`.
void updateOutlines(EntityManager& em, const Config& cfg);

} // namespace world_items
