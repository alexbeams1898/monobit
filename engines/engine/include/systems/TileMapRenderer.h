#pragma once

#include "TextureManager.h"
#include "TileMap.h"

// ---------------------------------------------------------------------------
// TileMapRenderer — draws the tile map as a background pass before RenderSystem.
//
// Uses its own shader + VAO/VBO with a static geometry approach:
//   init()   — compile shader, create VAO/VBO (empty)
//   upload() — bake all tile quads into a single static VBO (called once after
//              map generation). Each tile gets UV coords into the tileset atlas.
//              Falls back to flat-colored quads if no tileset is configured.
//   render() — one glDrawArrays call per frame (camera projection + tileset bind)
//   shutdown() — delete GL resources
// ---------------------------------------------------------------------------
class TileMapRenderer
{
  public:
    static void init();
    static void upload(const TileMap& map, const TileConfig& config, TextureManager& tm);
    static void clear(); // zero vertex count so nothing renders
    // Draws the GROUND layer (call BEFORE character sprites).
    static void render(float camX, float camY, int windowW, int windowH, float zoom = 1.0f);
    // Draws the DECORATION layer (flowers/tufts the character walks ON). Call AFTER
    // ground but BEFORE the character render pass. No-op if there's no such layer.
    static void renderDecoration(float camX, float camY, int windowW, int windowH,
                                 float zoom = 1.0f);
    // Draws the OVERHANG layer (props above characters -> walk-behind). Call AFTER
    // the character render pass. No-op if the map has no overhang layer.
    static void renderOverhang(float camX, float camY, int windowW, int windowH, float zoom = 1.0f);
    static void shutdown();
};
