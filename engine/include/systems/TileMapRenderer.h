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
    static void render(float camX, float camY, int windowW, int windowH);
    static void shutdown();
};
