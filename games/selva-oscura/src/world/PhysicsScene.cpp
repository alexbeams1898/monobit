#include "world/PhysicsScene.h"

#include "world/CryptLayout.h"
#include "world/StaticMeshAssets.h"
#include "world/StructureFootprints.h"
#include "world/Terrain.h"
#include "world/TerrainModifiers.h"

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

bool initPhysicsScene()
{
    if (sInit)
        return true;
    engine::physics::initPhysics();
    // Terrain + chapel are scene-owned. JsonScene::commitPrepared
    // inserts the terrain trimesh body (when the scene's JSON
    // declares a `terrain` field) and the static-mesh bodies on
    // activation; engine removes them on deactivation. This module
    // no longer owns any world bodies — only the player character.
    sInit = true;
    return true;
}

} // namespace selva::world

// crypt_layout::registerChapelTerrainModifiers — registers the chapel's
// terrain deformations with the engine TerrainModifiers registry.
// MUST run BEFORE world::initTerrain() builds the mesh (the modifier
// query is per-vertex during mesh build). See main.cpp init order.
namespace selva::world::crypt_layout
{

void registerChapelTerrainModifiers()
{
    using engine::world::StructureFootprint;
    using engine::world::TerrainModifier;

    // Doctrine [[feedback_structures_own_terrain_seam]]: chapel OWNS
    // its ground via three coordinated pieces:
    //   1) FlushAt modifier — terrain depresses to plinth-BOTTOM Y
    //      inside an exterior plateau footprint around the chapel,
    //      with a soft blend pad on the lateral / back sides so
    //      natural terrain ramps smoothly into the plateau.
    //   2) StructureFootprint Hole inside the chapel body — terrain
    //      doesn't render or collide inside (chapel floor mesh IS
    //      the floor).
    //   3) Foundation skirt primitive (build_foundation_skirt in
    //      gen_crypt_foundation.py) — vertical stone wall from plinth
    //      bottom down to deep below any plausible terrain, absorbing
    //      any boundary drift during the blend pad transition.
    //
    // The plateau flushes to (chapel-floor-Y - plinth-height) so the
    // visible plinth's bottom face sits exactly on terrain. The skirt
    // sits inside the plateau (its top is at plinth-bottom + zfs,
    // buried in the plinth) and only becomes visible where the blend
    // pad ramps terrain back down to natural Y — the skirt fills any
    // visible gap in that transition zone.

    // 1) Exterior plateau: terrain meets plinth bottom around the
    //    chapel. Extended forward by the door-threshold ramp length
    //    so terrain covers under the ramp (otherwise the ramp's bottom
    //    face is exposed as a void in front of the door). Soft 2m
    //    blend on -X/+X/-Z, SHARP +Z edge at the threshold OUTER face.
    constexpr float kDoorThresholdRamp = 0.50f;
    {
        TerrainModifier m;
        const float front_edge = kCryptZ + kHalfLength + kDoorThresholdRamp;
        const float back_edge = kCryptZ - kHalfLength;
        m.center_xz = {kCryptX, (front_edge + back_edge) * 0.5f};
        m.half_extents_xz = {kHalfWidth, (front_edge - back_edge) * 0.5f};
        m.mode = TerrainModifier::Mode::FlushAt;
        m.value = kChapelGroundY - kPlinthHeight; // plinth-bottom Y; plinth shows above
        m.blend_pad = 2.0f;
        m.blend_pad_pos_z = 0.0f; // sharp +Z edge at threshold outer face
        m.debug_name = "chapel_exterior_plateau";
        engine::world::registerTerrainModifier(m);
    }

    // Corridor geometry shared by footprint + slope modifiers.
    constexpr float kCorridorCeilingChapelLocalY =
        1.58f; // chapel-local Y of corridor ceiling top at chapel-side end
    constexpr float kStairRise = 0.16f;
    constexpr float kStairTread = 0.35f;
    constexpr float kCorridorSlopeMpM = kStairRise / kStairTread; // 0.46
    constexpr float kNaturalTerrainY = 32.0f;
    const float corridor_top_world_y_at_back_wall =
        (kChapelGroundY - kPlinthHeight) + kCorridorCeilingChapelLocalY; // 33.58
    const float dist_until_buried =
        (corridor_top_world_y_at_back_wall - kNaturalTerrainY) / kCorridorSlopeMpM; // 3.43m

    // 2) Structure footprint: chapel body only. Hole removes terrain
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
    constexpr float kTerrainQuadSpacing = 512.0f / 384.0f; // 1.333m at subdivide=384
    {
        const float front_edge_z = kCryptZ + kHalfLength - kTerrainQuadSpacing;
        const float back_edge_z = (kCryptZ - kHalfLength) - kTerrainQuadSpacing;
        StructureFootprint f;
        f.center_xz = {kCryptX, (front_edge_z + back_edge_z) * 0.5f};
        f.half_extents_xz = {kHalfWidth, (front_edge_z - back_edge_z) * 0.5f};
        f.debug_name = "chapel_footprint";
        engine::world::registerStructureFootprint(f);
    }

    // 3) Corridor wedge FlushSlope: terrain follows the corridor
    //    ceiling roof down for kCorridorWedgeRun meters past the
    //    chapel back wall, so the visible wedge gap between the
    //    chapel back wall and natural terrain is filled by terrain
    //    sloping along the corridor roof. Past this rect's back
    //    edge, the corridor is naturally buried by natural-Y terrain.
    {
        constexpr float kCorridorWedgeRun =
            4.0f; // a bit past dist_until_buried (3.43) so blend has room
        const float near_z = kCryptZ - kHalfLength;     // -214 (chapel back wall)
        const float far_z = near_z - kCorridorWedgeRun; // -218
        TerrainModifier m;
        m.center_xz = {kCryptX, (near_z + far_z) * 0.5f};
        m.half_extents_xz = {kHalfWidth, kCorridorWedgeRun * 0.5f};
        m.mode = TerrainModifier::Mode::FlushSlope;
        m.slope_axis = 2; // Z axis
        // FlushSlope interpolates from `value` at the -axis (-Z) edge
        // to `value_far` at the +axis (+Z) edge. Our -Z edge is FAR
        // from chapel (deeper, lower corridor Y); +Z edge is at
        // chapel back wall (highest corridor Y).
        m.value = corridor_top_world_y_at_back_wall -
                  kCorridorWedgeRun * kCorridorSlopeMpM; // ~31.74 at far_z
        m.value_far = corridor_top_world_y_at_back_wall; // 33.58 at near_z
        m.blend_pad = 0.0f; // sharp +Z edge (butts to chapel footprint Hole); sharp -Z edge fine
                            // too (natural Y already similar)
        m.debug_name = "chapel_corridor_wedge_slope";
        engine::world::registerTerrainModifier(m);
    }
}

} // namespace selva::world::crypt_layout

// Reopen selva::world for the rest of the translation unit's symbols.
namespace selva::world
{

void shutdownPhysicsScene()
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
