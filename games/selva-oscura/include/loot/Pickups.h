#pragma once

#include "ecs/Items.h"
#include "interact/Interaction.h"

#include <glm/vec3.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace selva::loot
{

using Id = std::uint64_t;
constexpr Id kInvalidId = 0;

// One pickup sitting in the world. Holds the ItemInstance that will
// be granted on interaction + the rarity/quality cached so the HUD
// glow can colorize without a registry lookup every frame. The
// interactable Id is stored so spawn / interact / despawn paths can
// keep the selva::interact registry in sync.
//
// `source_actor_id` ties the pickup to the corpse it dropped from:
// the pickup follows the corpse's hips joint as the death clip plays
// (matches Elden Ring's loot-stays-on-the-body convention). When the
// corpse vanishes from the actor pool, the pickup despawns with it.
// The pickup belongs to the corpse, period. Empty string disables
// the binding (used for world-container drops once those land); in
// that case `world_pos` is the static position. Multi-drop overlap
// is resolved by the interaction-cycle system, not by scattering
// world positions.
struct Pickup
{
    Id id = kInvalidId;
    glm::vec3 world_pos{0.0f, 0.0f, 0.0f};
    engine::ecs::ItemInstance item;
    engine::ecs::Rarity rarity = engine::ecs::Rarity::Common;
    engine::ecs::QualityTier quality = engine::ecs::QualityTier::Common;
    selva::interact::Id interact_id = selva::interact::kInvalidId;
    std::string source_actor_id;
    std::function<void()> on_granted;
};

// Resolve the live world position of a pickup. For source-bound
// pickups: hips joint of the source actor + xz_offset (so the glow
// rides the body as the death clip plays out). For unbound pickups
// (or when the source actor is gone): the cached world_pos. Shared
// by the HUD glow renderer + the interactable position closure so
// the E-prompt range check and the visual stay in lockstep.
glm::vec3 livePickupPos(const Pickup& p);

// Optional callback fired after a pickup has been granted to the
// player's inventory (toast + addItem succeeded). Used by callers
// that need to record per-grant state -- the descent-stair starter
// dispenser sets a flag on the active PlayerProfile so the starter
// doesn't re-spawn for that character. Empty function = no hook.
using OnGranted = std::function<void()>;

// Drop a pickup in the world at `world_pos` (already scattered +
// ground-clamped by the caller). Registers a Pickup interactable
// with the selva::interact system so the existing E-prompt works.
// `item` is the inventory entry granted on pickup; rarity/quality
// are resolved at spawn from the engine ItemRegistry and cached on
// the Pickup for HUD coloring. `source_actor_id` ties the pickup
// to a specific corpse for lifetime tracking; empty = permanent
// (no auto-despawn). `on_granted` fires after a successful grant;
// empty = no hook. Returns the new pickup Id, or kInvalidId if
// the item's config_path is unknown to the registry.
Id spawnPickup(const glm::vec3& world_pos, const engine::ecs::ItemInstance& item,
               const std::string& source_actor_id = std::string{},
               OnGranted on_granted = OnGranted{});

// Per-frame: drop pickups whose source corpse has vanished (faded
// completely, or no longer exists in the actor pool). Called from
// PerFrameTick. Pickups without a source_actor_id never despawn
// here.
void tickPickups();

// All currently live pickups. Read-only view for the HUD pass.
const std::vector<Pickup>& allPickups();

// Wipe every pickup + unregister all interactables. Called from
// hardResetWorldForCharacter -- pickups are mid-cycle dynamic state
// and don't survive character switches per the rebuild-from-authored
// reset doctrine.
void hardReset();

} // namespace selva::loot
