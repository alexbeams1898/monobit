#pragma once

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace selva::world
{

// One region of terrain. v1 = single chunk per region; the chunking
// architecture will subdivide this when streaming ships.
struct TerrainRegion
{
    std::string name;
    std::uint32_t vao = 0;
    std::uint32_t vbo = 0;
    std::uint32_t ebo = 0;
    int index_count = 0;
    // Heightmap data kept on CPU for sampling queries.
    std::vector<float> heights; // height[y * width + x] in world Y meters
    int hm_width = 0;
    int hm_height = 0;
    glm::vec2 world_origin{0.0f, 0.0f};
    float world_extent = 0.0f;
    float height_min = 0.0f;
    float height_max = 0.0f;
    // World-space Y offset added to every sampled height. Lets a
    // region's PNG encode heights in any range (typically near 0) and
    // be placed anywhere in world Y by setting this offset. Selva
    // surface uses 0 (heightmap encodes its world Y directly);
    // underground layers (Limbo, etc.) use large negative offsets so
    // the same PNG-encoding can place their terrain dozens of meters
    // underground without losing PNG precision.
    float y_offset = 0.0f;
    float base_color[3] = {0.16f, 0.13f, 0.10f};
    // Two-tone palette for the terrain shader: `tone_dark` is read on
    // flat / shaded ground; `tone_light` on slopes / exposed faces.
    // The shader mixes between them by computed dryness. Defaults
    // match the Selva surface "unstained wood floor" palette;
    // underground regions override in config.json with their own
    // colors (Limbo: near-black stone; deeper circles: per Dante's
    // descriptions).
    float tone_dark[3] = {0.08f, 0.07f, 0.06f};
    float tone_light[3] = {0.20f, 0.13f, 0.09f};
    // Footstep SFX bank to play when an actor's foot lands on this
    // region. Names match audio.json sound IDs. Default is the Selva
    // surface bank; underground regions override.
    std::string footstep_sound_id = "footstep_grass";
    // Mesh-vertex Y values, mirroring the GPU mesh. Used by sampleHeight
    // so the gameplay ground always matches the rendered surface.
    std::vector<float> mesh_y; // mesh_y[iz * verts_per_side + ix]
    int subdivide = 0;
    // CPU copies of the mesh geometry (positions + triangle indices) so
    // the physics layer can register this region as a static trimesh
    // body. World-space; ready to hand to Jolt directly.
    std::vector<glm::vec3> cpu_positions;
    std::vector<std::uint32_t> cpu_indices;
};

bool initTerrain();
void shutdownTerrain();

int terrainRegionCount();
const TerrainRegion& terrainRegion(int idx);

// Find the terrain region whose XZ AABB contains (world_x, world_z).
// Returns nullptr if no region matches (the XZ is outside all loaded
// regions). Iterates regions in registration order — same priority as
// sampleHeight. Use this to filter foliage / spawn / region-specific
// gameplay rules.
const TerrainRegion* terrainRegionAt(float world_x, float world_z);

// Sample world Y at the given world (x, z). Bilinear interpolation
// across the heightmap. Returns 0 if (x, z) falls outside any
// region. v1 assumes one region; expand to nearest-region or
// region-by-coordinate lookup when more ship.
float sampleHeight(float world_x, float world_z);

// Sample the actor's GROUND Y at (x, z): the highest of (a) the
// terrain sample and (b) the top of any walkable BoxCollider whose
// XZ footprint contains the query point. Used by gameplay to place
// the player / enemies on stairs, raised platforms, etc. — anywhere
// authored geometry sits above the terrain surface.
//
// Optional `current_y` lets callers prefer a ground at or below
// their current Y (so walking off the edge of a step drops to the
// next step, not up to an unrelated platform at the same XZ that
// happens to be higher). Pass -inf to disable the preference.
float groundHeight(float world_x, float world_z,
                   float current_y = -std::numeric_limits<float>::infinity());

// Player spawn position in world coordinates, loaded from the
// terrain config (player_spawn { x, z }). Y is intentionally NOT
// part of the config - callers should query sampleHeight(x, z) to
// place the player on the ground, so the spawn always tracks the
// current heightmap. Returns (0, 0) if the config is missing the
// player_spawn block.
glm::vec2 playerSpawnXZ();

} // namespace selva::world
