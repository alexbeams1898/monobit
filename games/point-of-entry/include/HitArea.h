#pragma once

#include <vector>

#include <entt/entt.hpp>

class EntityManager;

// An area that hurts what is inside it.
//
// Every attack in the game is one of these, whether it appeared beside the player or travelled
// there. It hits EVERYTHING it overlaps, once each -- the already-hit list is the difference
// between this and a melee swing that stops at the first target, and it is what lets one
// trigger pull clear a crowd.
//
// One-shot areas live a single tick; lingering ones (a cloud, a puddle) persist and keep
// catching things that walk in, which is why the list is a set of who has been hit rather than
// a flag saying whether anything was.
struct HitArea
{
    float radius = 0.0f;
    float damage = 0.0f;
    float remaining = 0.0f; // seconds left alive; expires at or below zero
    entt::entity owner = entt::null;

    // Travel, for a thrown area. Zero speed is one that simply sits where it appeared.
    float dir_x = 0.0f;
    float dir_y = 0.0f;
    float speed = 0.0f;
    float range_left = 0.0f;

    // Everything already hurt by this area. Small by construction -- an area lives for a moment
    // and touches what is in reach, not the whole floor.
    std::vector<entt::entity> hit;
};

namespace hit_area
{

// Move travelling areas, apply damage to whatever is newly inside them, and retire the expired.
void update(EntityManager& em, float dt);

} // namespace hit_area
