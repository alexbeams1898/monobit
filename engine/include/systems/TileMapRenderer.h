#pragma once

#include "TileMap.h"

// ---------------------------------------------------------------------------
// TileMapRenderer — draws the tile map as a background pass before RenderSystem.
//
// Uses its own shader + VAO/VBO with a static geometry approach:
//   init()   — compile shader, create VAO/VBO (empty)
//   upload() — bake all tile quads into a single static VBO (called once after
//              map generation). 80×60 tiles × 6 verts × 6 floats ≈ 675KB on GPU.
//   render() — one glDrawArrays call per frame (camera projection only)
//   shutdown() — delete GL resources
//
// Tile colors are baked at upload time using tileTypeTint():
//   Floor     — dark gray       (0.20, 0.20, 0.20)
//   Wall      — near-black warm (0.15, 0.12, 0.10)
//   DoorFrame — mid warm gray   (0.35, 0.30, 0.25)
//   Obstacle  — brown-gray      (0.25, 0.18, 0.12)
//
// When tile sprites are added, call upload() again to rebuild the VBO with
// UV coordinates. See issue #31 for chunked VBO upgrade path.
// ---------------------------------------------------------------------------
class TileMapRenderer
{
  public:
    static void init();
    static void upload(const TileMap& map);
    static void render(float camX, float camY, int windowW, int windowH);
    static void shutdown();
};
