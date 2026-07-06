#include "world/PhysicsRegion.h"

#include "world/CryptLayout.h"
#include "world/Lights.h"
#include "world/StaticMeshAssets.h"
#include "world/StructureFootprints.h"
#include "world/Terrain.h"
#include "world/TerrainModifiers.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace selva::world
{
namespace
{
bool sInit = false;
std::vector<engine::physics::BodyHandle> sStaticBodies;
engine::physics::BodyHandle sPlayer{0};

// Init-time stats captured for the F1-toggled physics-debug.log dump.
struct InitStats
{
    int terrain_regions = 0;
    int terrain_bodies = 0;
    int chapel_prims = 0;
    int chapel_bodies = 0;
    std::size_t total_tris = 0;
};
InitStats sStats;

constexpr float kPlayerRadius = 0.35f;
constexpr float kPlayerHeight = 1.80f;

} // namespace

void writeInitStatsToLog(FILE* f)
{
    if (f == nullptr)
        return;
    std::fprintf(f,
                 "[init] terrain_regions=%d terrain_bodies=%d "
                 "chapel_prims=%d chapel_bodies=%d total_tris=%zu "
                 "total_static_bodies=%zu\n",
                 sStats.terrain_regions, sStats.terrain_bodies, sStats.chapel_prims,
                 sStats.chapel_bodies, sStats.total_tris, sStaticBodies.size());
    std::fflush(f);
}

bool initPhysicsRegion()
{
    if (sInit)
        return true;
    engine::physics::initPhysics();
    // Terrain + chapel are region-owned. JsonRegion::commitPrepared
    // inserts the terrain trimesh body (when the region's JSON
    // declares a `terrain` field) and the static-mesh bodies on
    // activation; engine removes them on deactivation. This module
    // no longer owns any world bodies — only the player character.
    sInit = true;
    return true;
}

} // namespace selva::world

// crypt_layout::registerChapelStructureFootprints — registers the
// chapel's StructureFootprints (Hole for the chapel body, descent
// corridor footprint with vertical_profile in Limbo). Footprints
// stay in C++ because the vertical_profile field carries 32 cells
// of computed wall_floor_y/ceiling_y values that don't map cleanly
// to JSON yet. Terrain modifiers (plateau, slope, Acheron trench)
// moved to JSON; see surface/region.json terrain_modifiers.
namespace selva::world::crypt_layout
{

void registerChapelStructureFootprints()
{
    using engine::world::StructureFootprint;

    // Doctrine [[feedback_structures_own_terrain_seam]]: chapel OWNS
    // its ground via three coordinated pieces:
    //   1) FlushAt + FlushSlope terrain modifiers (now authored in
    //      surface/region.json's terrain_modifiers array, registered
    //      by loadAllRegionsRegister() at boot).
    //   2) StructureFootprint Hole inside the chapel body — terrain
    //      doesn't render or collide inside (chapel floor mesh IS
    //      the floor). Footprints stay in C++ because they carry
    //      computed vertical_profile data that doesn't map cleanly
    //      to JSON yet.
    //   3) Foundation skirt primitive (build_foundation_skirt in
    //      gen_chapel_and_descent.py) — vertical stone wall from plinth
    //      bottom down to deep below any plausible terrain, absorbing
    //      any boundary drift during the blend pad transition.

    // Structure footprint: chapel body only. Hole removes terrain
    //    inside the chapel. Behind the chapel, terrain is handled by
    //    the FlushSlope below — terrain follows the corridor roof
    //    down for a few meters, then natural terrain takes over.
    //
    //    Front edge pulled BACK by one terrain quad spacing so the
    //    "in front of door" terrain quad survives (chapel floor mesh
    //    covers it visually).
    //
    //    Back edge pushed FORWARD by one terrain quad spacing so
    //    surviving boundary quads' centroids are at Z < (chapel back
    //    wall - quad_spacing). Their vertices then sit fully INSIDE
    //    the wedge FlushSlope rect (where Y rides the corridor roof
    //    down) instead of straddling the boundary between depressed
    //    plateau Y and slope Y. Without this margin, the boundary
    //    surviving quad has one vertex inside the plateau (Y=32.25 =
    //    chapel ground, depressed) and one vertex inside the wedge
    //    slope (Y=33+) — producing a tilted triangle that floats
    //    INSIDE the corridor airspace and blocks the player descent
    //    (verified by physics-debug.log contact at Y=32.25 in the
    //    corridor mouth area).
    constexpr float kTerrainQuadSpacing = 1024.0f / 384.0f; // 2.667m at subdivide=384, extent=1024
    {
        const float front_edge_z = kCryptZ + kHalfLength - kTerrainQuadSpacing;
        const float back_edge_z = (kCryptZ - kHalfLength) - kTerrainQuadSpacing;
        StructureFootprint f;
        f.center_xz = {kCryptX, (front_edge_z + back_edge_z) * 0.5f};
        f.half_extents_xz = {kHalfWidth, (front_edge_z - back_edge_z) * 0.5f};
        f.region_name = "selva_inner";
        f.cuts_floor = true; // chapel body removes selva ground inside its rect
        f.debug_name = "chapel_footprint";
        engine::world::registerStructureFootprint(f);
    }

    // 2b) Descent corridor footprint in Limbo. cuts_rim + cuts_ceiling
    //     + cuts_wall (NOT cuts_floor — the corridor lands ON Limbo
    //     floor). The vertical_profile below tells cavern surfaces
    //     exactly where the corridor mesh sits; without it they'd cut
    //     the whole 2D rect from floor to ceiling.
    {
        constexpr float kCorridorHalfW = 2.4f;
        constexpr float kCorridorWallThickness = 0.6f;
        constexpr float kCorridorXHalf = kCorridorHalfW + kCorridorWallThickness;
        constexpr float kLimboRimBlend =
            12.0f; // mirror LIMBO_RIM_BLEND in gen_terrain_heightmap.py
        // Entry extends past rim outer edge so the cut covers the whole
        // rim_blend strip. Exit is the corridor mesh's last-step Z
        // (empirical from chapel_interior.glb AABB).
        constexpr float kCorridorEntryZ = -321.15f + kLimboRimBlend;
        constexpr float kCorridorExitZ = -353.15f;
        // Reference dimensions (kept as documentation; the corridor
        // collider derives its geometry from a region OBB authored in
        // JSON, not from these constants).
        [[maybe_unused]] constexpr float kCorridorCeilingClearance = 4.0f;
        [[maybe_unused]] constexpr float kLimboFloorY = -43.13f;
        [[maybe_unused]] constexpr float kCorridorSlopePerM =
            0.16f / 0.35f; // STAIR_RISE / STAIR_TREAD

        const float center_z = (kCorridorEntryZ + kCorridorExitZ) * 0.5f;
        const float half_z = (kCorridorEntryZ - kCorridorExitZ) * 0.5f;

        StructureFootprint f;
        f.center_xz = {kCryptX, center_z};
        f.half_extents_xz = {kCorridorXHalf, half_z};
        f.region_name = "limbo";
        f.cuts_floor = false;
        f.cuts_rim = true;
        f.cuts_ceiling = true;
        f.cuts_wall = true;

        // Vertical profile: corridor's true outer Y-range per Z.
        // Endpoints derived from chapel_interior.glb mesh AABBs (not
        // formula — the wedge-ramp ceiling slope drifts from the step
        // slope by ~0.4m, formulas can't catch that).
        constexpr int kNz = 32;
        constexpr float kStepZChapel = -213.15f;
        constexpr float kStepZLimbo = kCorridorExitZ; // -353.15
        constexpr float kCeilYChapel = 25.08f;
        constexpr float kCeilYLimbo = -39.14f;
        constexpr float kWallFloorYLimbo = -43.64f;     // wedge wall Y bottom at limbo end
        constexpr float kCorridorSlope = 0.16f / 0.35f; // ~0.4571 Y per Z
        f.vertical_profile.nx = 1;
        f.vertical_profile.nz = kNz;
        f.vertical_profile.cells.resize(kNz);
        for (int i = 0; i < kNz; ++i)
        {
            const float t = static_cast<float>(i) / static_cast<float>(kNz - 1);
            const float z = kCorridorExitZ + t * (kCorridorEntryZ - kCorridorExitZ);
            const float step_t = (z - kStepZLimbo) / (kStepZChapel - kStepZLimbo);
            const float ceiling_y = kCeilYLimbo + step_t * (kCeilYChapel - kCeilYLimbo);
            const float wall_floor_y = kWallFloorYLimbo + (z - kStepZLimbo) * kCorridorSlope;
            f.vertical_profile.cells[static_cast<size_t>(i)] = {wall_floor_y, ceiling_y};
        }

        f.debug_name = "descent_corridor_in_limbo";
        engine::world::registerStructureFootprint(f);
    }
}

// Terrain modifiers + Limbo lights moved to region.json
// (terrain_modifiers[] / props[]). StructureFootprints stay in C++
// until vertical_profile gets a schema.

void registerAuthoredWorld()
{
    registerChapelStructureFootprints();
}

} // namespace selva::world::crypt_layout

// Reopen selva::world for the rest of the translation unit's symbols.
namespace selva::world
{

void shutdownPhysicsRegion()
{
    if (!sInit)
        return;
    if (sPlayer != engine::physics::kInvalidBody)
    {
        engine::physics::removeBody(sPlayer);
        sPlayer = engine::physics::kInvalidBody;
    }
    for (auto h : sStaticBodies)
        engine::physics::removeBody(h);
    sStaticBodies.clear();
    engine::physics::shutdownPhysics();
    sInit = false;
}

engine::physics::BodyHandle playerBody()
{
    return sPlayer;
}

engine::physics::BodyHandle createPlayerBody(const glm::vec3& spawn_position)
{
    if (sPlayer != engine::physics::kInvalidBody)
        return sPlayer;
    sPlayer = engine::physics::addCharacter(spawn_position, kPlayerRadius, kPlayerHeight);
    return sPlayer;
}

engine::physics::BodyHandle createCharacterBody(const glm::vec3& spawn_position, float radius,
                                                float height,
                                                engine::physics::CharacterCollision collision)
{
    return engine::physics::addCharacter(spawn_position, radius, height, collision);
}

void destroyCharacterBody(engine::physics::BodyHandle handle)
{
    if (handle == engine::physics::kInvalidBody)
        return;
    engine::physics::removeBody(handle);
}

} // namespace selva::world
