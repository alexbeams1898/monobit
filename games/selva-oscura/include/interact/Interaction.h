#pragma once

#include "interact/Kinds.h"

#include <glm/vec3.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace selva::interact
{

using Id = std::uint32_t;
constexpr Id kInvalidId = 0;

// One thing the player can press E on while close enough. Position
// and availability are CLOSURES so the system reads live state every
// frame (NPCs move, items might become unavailable, etc.).
struct Decl
{
    Kind kind = Kind::Custom;
    std::function<glm::vec3()> position;
    float range_meters = 2.5f;
    std::string label;                 // noun, e.g. "The Guide", "Sangue Vial"
    std::function<void()> on_interact; // fires on E press
    std::function<bool()> available;   // optional gate; nullptr / true = always
};

// Snapshot of the currently-targeted interactable for UI rendering.
// Pointer is invalidated on tick(); copy if you need to hold it.
struct TargetView
{
    Id id;
    Kind kind;
    std::string label;
    glm::vec3 world_pos;
};

// Register a new interactable. Returns a stable Id; pass it to
// unregister() when the interactable disappears.
Id registerInteractable(Decl decl);

// Remove a previously-registered interactable. Safe to call with
// kInvalidId (no-op) or with an Id that's already removed (no-op).
void unregisterInteractable(Id id);

// Per-frame: scan registered interactables, collect every one that
// is (a) within its range of the player XZ, (b) available() returns
// true. Sorted by ascending XZ distance from the player. The first
// entry is the default focus; cycle() advances through the rest.
// Selection is sticky across frames: as long as the previously
// focused interactable is still in the list, it stays focused even
// if its distance ordering changes. When it falls out of the list
// (out of range / unregistered / no longer available), focus snaps
// to the closest candidate. Suppressed entirely when a Scene is
// active (cinematic moments own the input).
void tick();

// Current focused interactable, or nullptr if none. UI reads this
// to render the prompt + the focus highlight.
const TargetView* currentTarget();

// All in-range candidates this frame, sorted closest-first. Used by
// the HUD to draw unfocused-candidate markers so the player can see
// what else they could cycle to.
const std::vector<TargetView>& currentCandidates();

// Index into currentCandidates() of the focused interactable, or -1
// if none. Used by the HUD to render the focus highlight on the
// right candidate.
int currentFocusIndex();

// Cycle focus to the next in-range candidate (wraps at the end).
// No-op when fewer than 2 candidates are in range -- the player has
// no meaningful choice. Called by the input layer on the cycle-key
// edge.
void cycleFocus();

// Fires on_interact for the current target. Called by the input
// layer on E-press edge while in Playing + no Scene + no dialog.
// No-op if no current target.
void triggerCurrent();

// Wipe the registry + current target. Called from
// hardResetWorldForCharacter so cross-character interactables don't
// leak.
void hardReset();

} // namespace selva::interact
