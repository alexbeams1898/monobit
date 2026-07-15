#pragma once

#include "Growth.h"
#include "Observations.h"

#include <string>
#include <vector>

#include <entt/entt.hpp>

class EntityManager;

// The unified interaction layer: ONE concept for everything the player can act on in the
// world -- observe a spot now; pick up an item, use a crafting station, open a door later.
// An Interactable is plain data (a kind + an AABB + a label + the id its action needs); a
// single InteractionSystem each frame resolves the ACTIVE one (nearest in reach, or under
// the cursor), and Space OR a click fires it. Adding a kind is a new enum value + one
// dispatch case -- not a new system. See docs/design/GAME-SYSTEMS.md.
namespace interaction
{

// What firing an interactable does. Observe is the first + only kind today; the others
// name where this generalizes so the shape is obviously right (they are NOT built yet).
enum class Kind
{
    Observe, // reveal the observation at target_id (observations::observe)
    // Pickup,  // collect an item into the satchel
    // Craft,   // open a crafting station
    // Open,    // a door / container
};

// A world thing the player can act on. Component on an entity that also has a Transform
// (its position). The box (w,h) around the Transform is the interaction zone; the player
// interacts when within interact_reach of it, or when the cursor is over it. Plain data
// (save-friendly): no behavior lives here -- the InteractionSystem dispatches on `kind`.
struct Interactable
{
    Kind kind = Kind::Observe;
    float w = 32.0f; // box size around the Transform (world px)
    float h = 32.0f;
    std::string prompt;  // player-facing verb for the highlight UI ("Observe", "Pick up")
    std::string target;  // the id the action needs (for Observe: the observable id)
    bool active = false; // set by the system each frame: is this the highlighted target?
};

// The player's targeting + firing intent for this frame, resolved from input by the game
// (proximity is keyboard, mouse_* is the cursor). Kept separate from the ECS so the
// resolution logic is testable without a registry.
struct Intent
{
    float px = 0.0f; // player world position
    float py = 0.0f;
    float mouse_x = 0.0f; // cursor world position
    float mouse_y = 0.0f;
    bool mouse_valid = false; // false if the cursor is off-window / no mouse this frame
    bool pressed = false;     // the interact key (Space) fired this frame
    bool clicked = false;     // the left mouse button fired this frame
};

// Which interactable (if any) is the active target for `intent`, and whether it should
// fire. Pure over a flat list -- the ECS system builds the list from the registry and
// applies the result. reach = interact_reach (proximity range); the cursor targets by
// containment (hover). Nearest-wins; a hover target beats a proximity target.
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
// and a new Kind adds a field here, not a parameter. `reach` is interact_reach (proximity
// range). References -- valid for the call only.
struct Context
{
    observations::State& obs;
    const growth::GrowthState& growth;
    const observations::RollRng& rng;
    float reach = 0.0f;
};

// What the system did this frame: which interactable (if any) FIRED, so the caller can do
// the kind-specific UI follow-up (an Observe fire opens the spot's action menu). Also the
// Spirit EXP the action earned. `fired` is false when nothing fired (only targeting ran).
struct Outcome
{
    bool fired = false;
    Kind kind = Kind::Observe;
    std::string target; // the fired interactable's target id (e.g. the observable id)
    int earned = 0;     // Spirit EXP from the action
};

// The per-frame ECS system: build candidates from view<Transform, Interactable>, resolve
// the active target, mark it `active` (for the highlight), and if firing, dispatch on its
// Kind (Observe -> observations::observe by id). Returns what fired (see Outcome).
Outcome update(EntityManager& em, const Intent& intent, const Context& ctx);

} // namespace interaction
