#include "gameplay/TerritoryClamp.h"

#include "gameplay/Actor.h"
#include "gameplay/Faction.h"
#include "world/Territory.h"

#include <glm/gtc/quaternion.hpp>

#include <cmath>

namespace selva::gameplay
{

namespace
{

// Find the foreign-owner territory (must be owned by `foreign_owner`)
// whose OBB contains world_pos. Writes the actor's local-frame coords
// into out_local. Returns nullptr if no such volume found (shouldn't
// happen if regionIdAtPosition just returned foreign_owner).
const engine::world::Territory* findContainingForeignVolume(const glm::vec3& world_pos,
                                                            const std::string& foreign_owner,
                                                            glm::vec3& out_local)
{
    for (int i = 0; i < engine::world::territoryCount(); ++i)
    {
        const auto& t = engine::world::territoryAt(i);
        if (t.owner_region_id != foreign_owner)
            continue;
        const glm::vec3 local = glm::conjugate(t.orientation) * (world_pos - t.center);
        if (local.x < -t.half_extents.x || local.x >= t.half_extents.x)
            continue;
        if (local.y < -t.half_extents.y || local.y >= t.half_extents.y)
            continue;
        if (local.z < -t.half_extents.z || local.z >= t.half_extents.z)
            continue;
        out_local = local;
        return &t;
    }
    return nullptr;
}

// Compute the world-space position obtained by pushing local_in out of
// the OBB along the nearest local-axis face, plus a small epsilon.
glm::vec3 nearestFaceExit(const engine::world::Territory& t, const glm::vec3& local_in)
{
    constexpr float kPushEpsilon = 0.01f; // 1 cm past the face
    const float targets[6] = {-t.half_extents.x - kPushEpsilon, t.half_extents.x + kPushEpsilon,
                              -t.half_extents.y - kPushEpsilon, t.half_extents.y + kPushEpsilon,
                              -t.half_extents.z - kPushEpsilon, t.half_extents.z + kPushEpsilon};
    int best_idx = 0;
    float best_abs = std::abs(targets[0] - local_in[0]);
    for (int i = 1; i < 6; ++i)
    {
        const float d = std::abs(targets[i] - local_in[i / 2]);
        if (d < best_abs)
        {
            best_abs = d;
            best_idx = i;
        }
    }
    glm::vec3 candidate = local_in;
    candidate[best_idx / 2] = targets[best_idx];
    return t.center + t.orientation * candidate;
}

} // namespace

void clampActorsToOwnTerritory()
{
    auto& pool = actors();
    for (auto& a : pool)
    {
        if (a.controller == Controller::Input)
            continue;
        if (a.spawn_region_id.empty() || a.is_dead)
            continue;
        // Form gate: only DamnedSoul is ring-bound. See header.
        if (a.form != Form::DamnedSoul)
            continue;
        const std::string& at = engine::world::regionIdAtPosition(a.pos);
        if (at == a.spawn_region_id || at.empty())
            continue;
        glm::vec3 winner_local{0.0f};
        const engine::world::Territory* winner =
            findContainingForeignVolume(a.pos, at, winner_local);
        if (winner == nullptr)
            continue;
        const glm::vec3 candidate_world = nearestFaceExit(*winner, winner_local);
        // Only commit if the push actually lands in own territory or
        // void (Jolt handles void). If it lands in another foreign,
        // skip (rare; would need cascading resolution).
        const std::string& after = engine::world::regionIdAtPosition(candidate_world);
        if (after.empty() || after == a.spawn_region_id)
        {
            a.pos = candidate_world;
            a.velocity_xz = glm::vec2(0.0f);
        }
    }
}

} // namespace selva::gameplay
