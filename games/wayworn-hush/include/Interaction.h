#pragma once

#include "Growth.h"
#include "Inventory.h"
#include "Loot.h"
#include "Psyche.h"

#include <string>
#include <vector>

#include <entt/entt.hpp>

class EntityManager;

// The unified interaction layer: ONE concept for everything the player can act on in the
// world. Every interactable is OBSERVABLE (examining it surfaces a reading -- the warm
// glimmer marks it) and/or ACTIONABLE (interacting DOES something concrete -- pick up an
// item, gather a node; the item's floor icon is the cue, no glow). A spot can be both: an
// observable-and-takeable thing is an observation whose deed list includes a "pick up" deed
// (so the action lives in the observation menu, no mixed-menu here). A pure material is
// actionable-only: interacting fires it DIRECTLY (fast looting), never a menu. A single
// InteractionSystem resolves the ACTIVE spot each frame; Space OR a click fires it. See
// docs/design/GAME-SYSTEMS.md.
namespace interaction
{

// A concrete, direct action an actionable-only spot performs on interact (no reading). A
// spot that is ALSO observable carries no direct action -- its "pick up" lives as a deed in
// the observation menu instead. Plain data (save-friendly); the system dispatches on `kind`.
enum class ActionKind
{
    None,   // not directly actionable (observe-only, or nothing)
    Pickup, // deposit the item at `target` into the satchel, then despawn
    Gather, // roll the loot table at `target` into the satchel, then despawn
    // Craft, Open ... later
};

// A world thing the player can act on. Component on an entity that also has a Transform (its
// position). The box (w,h) around the Transform is the interaction zone; the player acts
// when within interact_reach, or when the cursor is over it. Two optional capabilities:
//   observe_id -- non-empty => examining surfaces this observation (glimmer marks it).
//   action     -- non-None  => interacting performs it directly (only when observe_id is
//                  empty; an encounter spot's "take" is a deed, not a direct action).
// Plain data (save-friendly): no behavior here -- the InteractionSystem routes.
struct Interactable
{
    float w = 32.0f; // box size around the Transform (world px)
    float h = 32.0f;
    // WHICH placed thing this is (see ldtk::PickupPlacement::placement_id). Carried so
    // that removing it for good can be recorded against the map -- without it, a thing
    // taken is forgotten the moment the entity dies, and comes back next visit.
    std::string placement_id;
    std::string observe_id;               // observation id (empty = not observable)
    ActionKind action = ActionKind::None; // direct action (None = not directly actionable)
    std::string target;                   // the action's id (item id / loot table id)
    bool active = false; // set by the system each frame: is this the highlighted target?
    // Is this interactable LIVE right now? A hidden encounter (visible_when unmet) is not:
    // it must offer NEITHER verb, so it isn't a target and doesn't glow. Set live by whoever
    // owns the reveal condition (glimmer::update, for encounters), read by the interaction
    // resolve + the glow. The ONE answer to "does this spot exist right now" -- so observe
    // and act can never disagree about it.
    bool present = true;
};

// The player's targeting + firing intent for this frame, resolved from input by the game
// (proximity is keyboard, mouse_* is the cursor). Kept separate from the ECS so the
// resolution logic is testable without a registry.
struct Intent
{
    float px = 0.0f; // player world position
    float py = 0.0f;
    // Which way the player is looking. With WASD, facing IS the pointer: you say
    // what you mean by turning toward it, so a spot behind you is not a target
    // (proximity only -- the cursor is its own pointing, see mouse_*).
    float face_dx = 0.0f;
    float face_dy = 1.0f;
    float mouse_x = 0.0f; // cursor world position
    float mouse_y = 0.0f;
    bool mouse_valid = false; // false if the cursor is off-window / no mouse this frame
    bool pressed = false;     // the interact key (Space) fired this frame
    bool clicked = false;     // the left mouse button fired this frame
    // The "act" modifier: interacting while RUNNING (Shift held) means ACT (skip straight to
    // the deed menu) rather than OBSERVE. Walking = observe (slow down to notice); running =
    // act (you're in motion, you know this spot). Only affects observable spots; items loot
    // the same either way.
    bool act = false;
};

// Which interactable (if any) is the active target for `intent`, and whether it should
// fire. Pure over a flat list -- the ECS system builds the list from the registry and
// applies the result. reach = interact_reach (proximity range); the cursor targets by
// containment (hover). Nearest-wins; a hover target beats a proximity target.
// A proximity target must also be IN FRONT of the player (see kFacingDot); a hovered
// one needn't be -- pointing at a thing is already saying which thing you mean.
struct Resolution
{
    int index = -1;    // into the provided list; -1 = nothing targeted
    bool fire = false; // the target should fire this frame (pressed, or clicked on hover)
};

// A candidate for resolution: an interactable's box in world space.
struct Candidate
{
    float cx = 0.0f; // box center
    float cy = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
};

// Resolve the active target + fire from the candidate list (pure; unit-tested). See .cpp.
Resolution resolve(const std::vector<Candidate>& items, const Intent& intent, float reach);

// The game-state an interactable's action needs to fire, bundled so update() stays small
// and a new capability adds a field here, not a parameter. `reach` is interact_reach
// (proximity range). References -- valid for the call only.
struct Context
{
    psyche::State& obs;
    const growth::GrowthState& growth;
    const psyche::RollRng& rng;
    inventory::Satchel& satchel;      // Pickup/Gather deposit here
    const inventory::Registry& items; // item blueprints (stackability, rarity, name)
    const loot::Registry& loot;       // loot tables (Gather rolls one)
    float reach = 0.0f;
};

// What the system did this frame, so the caller can do the follow-up. Exactly one path
// fires: OBSERVE (observe_target set -> the caller opens that spot's reading/action menu) or
// a DIRECT ACTION (observe_target empty, items set -> the caller toasts the already-deposited
// find). `fired` is false when nothing fired (only targeting ran).
struct Outcome
{
    bool fired = false;
    std::string observe_target; // the fired observable spot's id (empty if a direct item fired)
    bool act = false;           // the fire was an ACT (running): skip the reading -> deed menu
    int earned = 0;             // Spirit EXP from an observe
    // Items a DIRECT action deposited, for the toast (Pickup -> one; Gather -> the handful).
    // Empty for an encounter (a "take" deed's grant flows through the menu, not here).
    std::vector<inventory::ItemInstance> items;
    // The placement id of a thing this fire removed from the world for good, if any. Passed
    // up as an opaque id (like the observation system's grants): interaction destroys the
    // entity, but only the GAME knows that a removal is something a save must remember.
    std::string removed_placement;
};

// The per-frame ECS system: build candidates from view<Transform, Interactable>, resolve
// the active target, mark it `active` (for the highlight), and if firing, route by capability
// (observe_id -> observeById; else action -> deposit + despawn). Returns what fired (Outcome).
Outcome update(EntityManager& em, const Intent& intent, const Context& ctx);

} // namespace interaction
