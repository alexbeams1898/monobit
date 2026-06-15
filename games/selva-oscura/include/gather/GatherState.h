#pragma once

#include "ecs/Items.h"

#include <cstdint>
#include <string>
#include <vector>

namespace selva::gather
{

// One spawned gather node's persistent state. The roll IS committed at
// spawn time per [[project_anti_cheese_rolls_locked_2026_06_14]] -- the
// quality field below was rolled the moment the node materialized and
// will yield exactly that quality on pickup. Quit-reload preserves the
// roll; the player can never reroll an observed outcome.
//
// Stored in PlayerProfile.active_gather_nodes; serialized as part of
// the save file. The gather spawner reads this list to determine the
// live world population and registers Interactables at the saved
// positions on region activation.
struct NodeState
{
    // Unique id assigned at spawn time. Stable across save/load so the
    // Interactable handle and the persistent list stay in sync without
    // pointer juggling. Allocated from a monotonic counter
    // (PlayerProfile::next_gather_node_id).
    std::uint32_t id = 0;

    // The gather node config this node was spawned from
    // (config/gather_nodes/<id>.json). With the unified Wood forage
    // flow, all live nodes share one flow config; this field keeps
    // the link to whichever flow they belong to.
    std::string node_config_path;

    // The material item this specific node yields when gathered
    // (config/items/materials/<material>.json). Rolled at spawn time
    // from the flow's rarity-weighted material pool; persisted per
    // anti-cheese. The renderer reads ItemDef.world_mesh via this
    // path; the grant gives an ItemInstance of this config_path.
    std::string material_config_path;

    // World-space position chosen procedurally at spawn. Y is the
    // terrain ground height at the (x, z) candidate.
    float pos_x = 0.0f;
    float pos_y = 0.0f;
    float pos_z = 0.0f;

    // Quality rolled at spawn time. Pickup grants an ItemInstance with
    // this exact quality stamped on it (anti-cheese commit-on-spawn).
    engine::ecs::QualityTier quality = engine::ecs::QualityTier::Common;

    // World-space Y rotation (radians) committed at spawn for visual
    // variety -- two adjacent bark scraps don't face the same way.
    // Also committed at spawn per anti-cheese (quit-reload preserves
    // pose, no reroll surface).
    float yaw = 0.0f;
};

// Per-gather-flow scheduler state. One entry per loaded gather node
// config (so bark / earth / lichen each have their own timer).
//
// Two-phase lifecycle:
//   - Initial fill: `initial_fill_done == false`. Spawner fast-spawns
//     (no interval gate) until live count reaches active_cap. Once
//     reached, sets initial_fill_done = true.
//   - Trickle: `initial_fill_done == true`. Subsequent spawns (player
//     gathered one, population dropped below cap) gated by
//     `wallClock() - last_spawn_wallclock >= respawn_seconds`.
//
// On cycle reset (hardResetWorldForCharacter / softResetWorldForCycle):
// flow state cleared; fresh initial fill on next tick.
struct FlowState
{
    std::string node_config_path;
    float last_spawn_wallclock = 0.0f;
    bool initial_fill_done = false;
};

} // namespace selva::gather
