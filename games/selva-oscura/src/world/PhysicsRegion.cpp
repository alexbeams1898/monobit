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
    //      the floor). Footprints stay in C++ for now because they
    //      carry computed vertical_profile data that doesn't map
    //      cleanly to JSON yet.
    //   3) Foundation skirt primitive (build_foundation_skirt in
    //      gen_crypt_foundation.py) — vertical stone wall from plinth
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
        constexpr float kCorridorCeilingClearance = 4.0f;
        constexpr float kLimboFloorY = -43.13f;
        constexpr float kCorridorSlopePerM = 0.16f / 0.35f; // STAIR_RISE / STAIR_TREAD

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

// Limbo's terrain modifiers (Acheron trench) moved to
// surface/region.json's terrain_modifiers array per the Commit 4
// dual-source-of-truth fix. When the limbo region.json lands in
// Commit 5, the trench moves into limbo/region.json (where it
// conceptually belongs).

void registerLimboLights()
{
    using engine::world::LightSource;

    // Limbo plateau top (must match surface/region.json terrain
    // y_offset for the "limbo" terrain region: -43.13).
    constexpr float kLimboTopY = -43.13f;
    // Sit each light ~0.8m above the ground — waist-height for a
    // brazier, lap-height for a fungus bowl. Authored as a single Y
    // for v1; future per-source meshes (lamp / brazier / fungus) will
    // each carry their own emitter height.
    constexpr float kLightY = kLimboTopY + 0.8f;

    // FAR shore = south of the Acheron trench (trench at Z=-500,
    // bank ends ~Z=-505). All sources sit at Z = -520 .. -880 (the
    // 360m-deep playable plain south of the river). The NEAR shore
    // (Z ~-321 to -495) where the descent stair lets out stays
    // unlit, so the player must craft a torch to see — and the
    // visible lights across the river read as the soul congregation
    // to head toward. Future Noble Castle placeholder will sit
    // roughly at the center of this lit zone.
    //
    // Tag region_name = "limbo" so the per-region filter in
    // WorldRenderer drops these for Selva-surface draws.

    auto makeLight = [&](float x, float z, glm::vec3 color, float intensity, float radius,
                         float flicker_amp, float flicker_freq, const char* name)
    {
        LightSource L;
        L.position = glm::vec3(x, kLightY, z);
        L.color = color;
        L.intensity = intensity;
        L.radius = radius;
        L.flicker_amp = flicker_amp;
        L.flicker_freq = flicker_freq;
        L.region_name = "limbo";
        L.debug_name = name;
        engine::world::registerLight(L);
    };

    constexpr glm::vec3 kWarm{1.00f, 0.55f, 0.22f};   // campfires / oil lamps
    constexpr glm::vec3 kFungus{0.40f, 0.85f, 0.45f}; // bioluminescent fungus
    constexpr float kFireAmp = 0.12f;
    constexpr float kFireFreq = 2.5f;
    constexpr float kFungusAmp = 0.06f;
    constexpr float kFungusFreq = 0.9f;

    // Scattered "encampments" weighted toward the future Castle area
    // (mid-far Limbo). Each main light has a smaller companion 4-15m
    // away so they read as gatherings, not isolated points.
    makeLight(-40.0f, -560.0f, kWarm, 0.8f, 11.0f, kFireAmp, kFireFreq, "limbo_campfire_a");
    makeLight(-32.0f, -566.0f, kWarm, 0.4f, 6.0f, kFireAmp, kFireFreq * 1.1f,
              "limbo_campfire_a_companion");

    makeLight(60.0f, -620.0f, kWarm, 0.7f, 10.0f, kFireAmp, kFireFreq, "limbo_campfire_b");
    makeLight(67.0f, -615.0f, kWarm, 0.35f, 5.5f, kFireAmp, kFireFreq * 0.9f,
              "limbo_campfire_b_companion");

    makeLight(-90.0f, -700.0f, kFungus, 0.5f, 9.0f, kFungusAmp, kFungusFreq, "limbo_fungus_a");
    makeLight(-83.0f, -707.0f, kFungus, 0.30f, 6.0f, kFungusAmp, kFungusFreq * 1.2f,
              "limbo_fungus_a_companion");

    makeLight(80.0f, -740.0f, kWarm, 0.6f, 12.0f, kFireAmp, kFireFreq, "limbo_brazier_a");
    makeLight(86.0f, -732.0f, kFungus, 0.25f, 5.0f, kFungusAmp, kFungusFreq * 0.8f,
              "limbo_brazier_a_fungus_neighbor");

    makeLight(0.0f, -780.0f, kWarm, 1.0f, 16.0f, kFireAmp * 1.2f, kFireFreq * 0.85f,
              "limbo_brazier_b");
    makeLight(-9.0f, -772.0f, kWarm, 0.4f, 7.0f, kFireAmp, kFireFreq, "limbo_brazier_b_companion");
    makeLight(12.0f, -786.0f, kFungus, 0.35f, 6.0f, kFungusAmp, kFungusFreq * 1.1f,
              "limbo_brazier_b_fungus");

    makeLight(120.0f, -650.0f, kFungus, 0.4f, 8.0f, kFungusAmp, kFungusFreq, "limbo_fungus_b");
    makeLight(127.0f, -657.0f, kFungus, 0.25f, 5.0f, kFungusAmp, kFungusFreq * 1.3f,
              "limbo_fungus_b_companion");

    makeLight(-130.0f, -820.0f, kWarm, 0.5f, 10.0f, kFireAmp, kFireFreq, "limbo_campfire_c");
    makeLight(-122.0f, -825.0f, kWarm, 0.30f, 5.5f, kFireAmp, kFireFreq * 1.15f,
              "limbo_campfire_c_companion");

    makeLight(30.0f, -870.0f, kFungus, 0.4f, 7.0f, kFungusAmp, kFungusFreq, "limbo_fungus_c");
    makeLight(38.0f, -863.0f, kFungus, 0.25f, 5.0f, kFungusAmp, kFungusFreq * 0.95f,
              "limbo_fungus_c_companion");
}

void registerAuthoredWorld()
{
    // Chapel + Acheron terrain modifiers moved to
    // surface/region.json's terrain_modifiers array (Commit 4 of the
    // region rebuild). Authored as data, parsed by JsonRegion, and
    // registered into the global modifier registry by
    // loadAllRegionsRegister() at boot before initTerrain(). See
    // [[feedback_dual_source_of_truth_is_the_bug]] for the doctrine.
    //
    // StructureFootprints (chapel body Hole + descent corridor in
    // Limbo) stay in C++ for now — they carry computed vertical-
    // profile data (32 cells per footprint) that doesn't map cleanly
    // to JSON yet. Promote when JSON schema for footprints is needed.
    //
    // Lights are still authored in C++ for now too -- they don't
    // have a JSON schema yet, and there's no boot-ordering pressure
    // to move them.
    registerChapelStructureFootprints();
    registerLimboLights();
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

} // namespace selva::world
