#include "world/PhysicsScene.h"

#include "world/CryptLayout.h"
#include "world/StaticMeshAssets.h"
#include "world/Terrain.h"
#include "world/TerrainModifiers.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace selva::world
{
namespace
{
bool                                     sInit = false;
std::vector<engine::physics::BodyHandle> sStaticBodies;
engine::physics::BodyHandle              sPlayer{0};

// Init-time stats captured for the F1-toggled physics-debug.log dump.
struct InitStats
{
    int         terrain_regions = 0;
    int         terrain_bodies  = 0;
    int         chapel_prims    = 0;
    int         chapel_bodies   = 0;
    std::size_t total_tris      = 0;
};
InitStats sStats;

constexpr float kPlayerRadius = 0.35f;
constexpr float kPlayerHeight = 1.80f;

} // namespace

void writeInitStatsToLog(FILE* f)
{
    if (f == nullptr) return;
    std::fprintf(f,
                 "[init] terrain_regions=%d terrain_bodies=%d "
                 "chapel_prims=%d chapel_bodies=%d total_tris=%zu "
                 "total_static_bodies=%zu\n",
                 sStats.terrain_regions, sStats.terrain_bodies,
                 sStats.chapel_prims, sStats.chapel_bodies,
                 sStats.total_tris, sStaticBodies.size());
    std::fflush(f);
}

bool initPhysicsScene()
{
    if (sInit) return true;
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
    using engine::world::TerrainModifier;

    // 1) Plateau under the chapel exterior + apse. Terrain rises to
    //    meet the plinth top inside the rect, with a 2m soft pad
    //    outside to ramp back down to natural elevation. The rect
    //    extends slightly past the chapel BACK to cover both the
    //    back-plinth slabs (which project ~0.9m behind the back
    //    wall) AND the apse half-cylinder (which projects 1.8m +
    //    a 5cm inset). Without this extension the plateau's blend_pad
    //    starts ramping terrain DOWN immediately at the back wall
    //    plane, leaving the back plinths + apse base floating over
    //    sloped natural terrain — visible as dark holes on either
    //    side of the apse.
    constexpr float kApseRearProjection = 1.8f + 0.05f;  // APSE_RADIUS + apse_inset
    constexpr float kPlateauRearExtra   = kApseRearProjection + 0.20f;  // safety margin
    {
        TerrainModifier m;
        // Shift the center BACK by half of the rear-extra so the
        // front edge stays at the chapel front (z=-206) — no
        // unintended terrain rise on the door side.
        m.center_xz = {kCryptX, kCryptZ - kPlateauRearExtra * 0.5f};
        m.half_extents_xz = {kHalfWidth, kHalfLength + kPlateauRearExtra * 0.5f};
        m.mode = TerrainModifier::Mode::FlushAt;
        m.value = kChapelGroundY;  // plinth bottom = exterior ground level
        m.blend_pad = 2.0f;
        m.debug_name = "chapel_plateau";
        engine::world::registerTerrainModifier(m);
    }

    // 2) Hole through terrain over the stair shaft. Drops terrain
    //    quads inside the rect so the player can fall through
    //    plateau-Y terrain into the descent. Required because the
    //    chapel mesh has NO authored interior floor: terrain at
    //    plateau Y IS the chapel's walkable floor everywhere
    //    EXCEPT the shaft.
    //
    //    Rect = upper-flight footprint + small margin so all quad
    //    centroids inside the shaft are dropped at the current
    //    terrain resolution (~2.7m/vertex).
    {
        // Carve ONLY the upper-flight shaft (the part of the descent
        // that intersects terrain at plateau Y). Past z=-214 the
        // corridor descends below plateau Y immediately and never
        // re-intersects terrain — the colle surface rises ABOVE the
        // corridor for the remaining 140m, so carving the full
        // descent footprint would punch holes through the colle
        // visible from above. Only the shaft mouth needs carving.
        const float shaft_z_chapel_near = kUpperFlightTopChapelLocalY;
        const float shaft_z_chapel_far  = kUpperFlightTopChapelLocalY + kUpperFlightRun;
        const float shaft_z_world_near = kCryptZ - shaft_z_chapel_near;
        const float shaft_z_world_far  = kCryptZ - shaft_z_chapel_far;
        constexpr float kQuadMargin = 0.5f;
        TerrainModifier m;
        m.center_xz = {kCryptX, (shaft_z_world_near + shaft_z_world_far) * 0.5f};
        m.half_extents_xz = {kSingleFlightHalfWidth + kQuadMargin,
                             std::abs(shaft_z_world_near - shaft_z_world_far) * 0.5f + kQuadMargin};
        m.mode = TerrainModifier::Mode::Hole;
        m.debug_name = "chapel_stair_shaft_hole";
        engine::world::registerTerrainModifier(m);
    }

    // NO descent strip / no terrain over the tunnel. The colle is
    // independent of the underground tunnel; tunnel descends into
    // rock beneath, invisible from above. If the tunnel ceiling slab
    // happens to poke above the colle surface where the colle isn't
    // tall enough, that's a CHAPEL MESH issue (lower the ceiling
    // there), not a terrain issue.
}

} // namespace selva::world::crypt_layout

// Reopen selva::world for the rest of the translation unit's symbols.
namespace selva::world
{

void shutdownPhysicsScene()
{
    if (!sInit) return;
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
