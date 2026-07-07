#include "world/TerrainModifiers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;
using engine::world::applyTerrainModifiers;
using engine::world::clearTerrainModifiers;
using engine::world::registerTerrainModifier;
using engine::world::TerrainModifier;
using engine::world::terrainModifierCount;

// ---------------------------------------------------------------------------
// TerrainModifiers tests — cover backward-compat rect modes AND the new
// PolygonFlushAt mode (concave shapes, per-edge blend pads, AABB rejection).
// ---------------------------------------------------------------------------

TEST_CASE("rect FlushAt still applies at center", "[terrain-modifiers][rect]")
{
    clearTerrainModifiers();
    TerrainModifier m;
    m.center_xz = {0.0f, 0.0f};
    m.half_extents_xz = {10.0f, 5.0f};
    m.mode = TerrainModifier::Mode::FlushAt;
    m.value = -3.0f;
    m.blend_pad = 0.0f;
    registerTerrainModifier(m);

    REQUIRE(applyTerrainModifiers(nullptr, 0.0f, 0.0f, 5.0f) == Approx(-3.0f));
    // Outside the rect, unmodified.
    REQUIRE(applyTerrainModifiers(nullptr, 100.0f, 0.0f, 5.0f) == Approx(5.0f));
}

TEST_CASE("polygon square applies inside, not outside", "[terrain-modifiers][polygon]")
{
    clearTerrainModifiers();
    TerrainModifier m;
    m.mode = TerrainModifier::Mode::PolygonFlushAt;
    m.value = -10.0f;
    m.blend_pad = 0.0f;
    // 20x20 square centered on origin (CCW: BL -> BR -> TR -> TL).
    m.polygon_vertices_xz = {{-10.0f, -10.0f}, {10.0f, -10.0f}, {10.0f, 10.0f}, {-10.0f, 10.0f}};
    m.debug_name = "test_square";
    registerTerrainModifier(m);

    REQUIRE(terrainModifierCount() == 1);
    // Inside → modifier applies.
    REQUIRE(applyTerrainModifiers(nullptr, 0.0f, 0.0f, 5.0f) == Approx(-10.0f));
    REQUIRE(applyTerrainModifiers(nullptr, 5.0f, 5.0f, 5.0f) == Approx(-10.0f));
    // Outside → unchanged (sharp edge, no blend pad).
    REQUIRE(applyTerrainModifiers(nullptr, 15.0f, 0.0f, 5.0f) == Approx(5.0f));
    REQUIRE(applyTerrainModifiers(nullptr, 0.0f, 100.0f, 5.0f) == Approx(5.0f));
}

TEST_CASE("polygon with blend pad ramps outside", "[terrain-modifiers][polygon]")
{
    clearTerrainModifiers();
    TerrainModifier m;
    m.mode = TerrainModifier::Mode::PolygonFlushAt;
    m.value = 0.0f;
    m.blend_pad = 5.0f;
    // 20x20 square.
    m.polygon_vertices_xz = {{-10.0f, -10.0f}, {10.0f, -10.0f}, {10.0f, 10.0f}, {-10.0f, 10.0f}};
    registerTerrainModifier(m);

    // Inside → weight 1.0 → Y forced to value=0.
    REQUIRE(applyTerrainModifiers(nullptr, 0.0f, 0.0f, 10.0f) == Approx(0.0f));
    // Just outside east edge (dist=2) → weight = 1 - smoothstep(2/5).
    // smoothstep(0.4) = 0.4^2 * (3 - 2*0.4) = 0.16 * 2.2 = 0.352.
    // weight = 1 - 0.352 = 0.648. blended = 10 * (1-0.648) + 0 * 0.648 = 10 * 0.352 = 3.52.
    const float actual = applyTerrainModifiers(nullptr, 12.0f, 0.0f, 10.0f);
    REQUIRE(actual == Approx(3.52f).margin(0.01f));
    // Far outside (dist=10 > pad=5) → weight 0 → unchanged.
    REQUIRE(applyTerrainModifiers(nullptr, 25.0f, 0.0f, 10.0f) == Approx(10.0f));
}

TEST_CASE("polygon concave L-shape", "[terrain-modifiers][polygon][concave]")
{
    clearTerrainModifiers();
    TerrainModifier m;
    m.mode = TerrainModifier::Mode::PolygonFlushAt;
    m.value = -2.0f;
    m.blend_pad = 0.0f;
    // Concave L-shape (CCW). Vertices form the outline:
    //   (0,0) -> (10,0) -> (10,5) -> (5,5) -> (5,10) -> (0,10) -> close
    // The notch in the top-right corner is OUTSIDE the polygon.
    m.polygon_vertices_xz = {{0.0f, 0.0f}, {10.0f, 0.0f}, {10.0f, 5.0f},
                             {5.0f, 5.0f}, {5.0f, 10.0f}, {0.0f, 10.0f}};
    registerTerrainModifier(m);

    // Inside the bottom bar of the L.
    REQUIRE(applyTerrainModifiers(nullptr, 5.0f, 2.5f, 100.0f) == Approx(-2.0f));
    // Inside the vertical bar of the L.
    REQUIRE(applyTerrainModifiers(nullptr, 2.5f, 7.5f, 100.0f) == Approx(-2.0f));
    // Inside the notch (the concave hole) — should be OUTSIDE the polygon.
    REQUIRE(applyTerrainModifiers(nullptr, 7.5f, 7.5f, 100.0f) == Approx(100.0f));
    // Well outside.
    REQUIRE(applyTerrainModifiers(nullptr, -5.0f, 5.0f, 100.0f) == Approx(100.0f));
}

TEST_CASE("polygon per-edge blend pads override scalar", "[terrain-modifiers][polygon]")
{
    clearTerrainModifiers();
    TerrainModifier m;
    m.mode = TerrainModifier::Mode::PolygonFlushAt;
    m.value = 0.0f;
    m.blend_pad = 0.0f; // scalar = sharp
    // 20x20 square. Edge order: 0=south (BL->BR), 1=east (BR->TR),
    //                          2=north (TR->TL), 3=west (TL->BL).
    m.polygon_vertices_xz = {{-10.0f, -10.0f}, {10.0f, -10.0f}, {10.0f, 10.0f}, {-10.0f, 10.0f}};
    // Only the east edge has a soft pad.
    m.polygon_edge_blend_pads = {-1.0f, 5.0f, -1.0f, -1.0f};
    registerTerrainModifier(m);

    // Just east of the east edge (nearest edge = east, pad=5) → ramp.
    // dist=2, weight = 1 - smoothstep(0.4) = 0.648.
    // blended = 10 * (1-0.648) + 0 * 0.648 = 3.52.
    REQUIRE(applyTerrainModifiers(nullptr, 12.0f, 0.0f, 10.0f) == Approx(3.52f).margin(0.01f));
    // Just south of the south edge (pad=0 fallback) → no blend.
    REQUIRE(applyTerrainModifiers(nullptr, 0.0f, -12.0f, 10.0f) == Approx(10.0f));
}

TEST_CASE("polygon rejects registration when <3 vertices", "[terrain-modifiers][polygon]")
{
    clearTerrainModifiers();
    TerrainModifier m;
    m.mode = TerrainModifier::Mode::PolygonFlushAt;
    m.value = 0.0f;
    m.polygon_vertices_xz = {{0.0f, 0.0f}, {1.0f, 0.0f}}; // only 2 verts
    registerTerrainModifier(m);
    REQUIRE(terrainModifierCount() == 0);
}

TEST_CASE("polygon rejects registration when edge_pads size mismatches",
          "[terrain-modifiers][polygon]")
{
    clearTerrainModifiers();
    TerrainModifier m;
    m.mode = TerrainModifier::Mode::PolygonFlushAt;
    m.value = 0.0f;
    m.polygon_vertices_xz = {{0.0f, 0.0f}, {1.0f, 0.0f}, {0.5f, 1.0f}};
    m.polygon_edge_blend_pads = {1.0f, 1.0f}; // wrong size (2 vs 3)
    registerTerrainModifier(m);
    REQUIRE(terrainModifierCount() == 0);
}

TEST_CASE("polygon AABB cache populated", "[terrain-modifiers][polygon]")
{
    clearTerrainModifiers();
    TerrainModifier m;
    m.mode = TerrainModifier::Mode::PolygonFlushAt;
    m.value = 0.0f;
    m.blend_pad = 2.5f;
    m.polygon_vertices_xz = {{-5.0f, -3.0f}, {7.0f, -3.0f}, {7.0f, 4.0f}, {-5.0f, 4.0f}};
    registerTerrainModifier(m);

    REQUIRE(terrainModifierCount() == 1);
    const auto& stored = engine::world::terrainModifierAt(0);
    REQUIRE(stored.polygon_aabb_min_x == Approx(-5.0f));
    REQUIRE(stored.polygon_aabb_max_x == Approx(7.0f));
    REQUIRE(stored.polygon_aabb_min_z == Approx(-3.0f));
    REQUIRE(stored.polygon_aabb_max_z == Approx(4.0f));
    REQUIRE(stored.polygon_max_blend_pad == Approx(2.5f));
}
