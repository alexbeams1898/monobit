#pragma once

class Engine;
class EntityManager;

// Internal pixel-art resolution. The world renders here, then integer-upscales
// to the window (see gl/PixelRenderTarget). 384x216 = 16:9, 24x13.5 tiles at
// 16px; ints scale cleanly to 1080p (x5). See docs/design/SCALE.md.
inline constexpr int kInternalWidth = 384;
inline constexpr int kInternalHeight = 216;

// Ambient background / letterbox color. Shared by the engine clear and the
// pixel-target clear so the internal image and the letterbox bars agree.
// Placeholder muted slate -- the palette is deliberately unresolved
// (docs/design/AESTHETIC.md), so this is a neutral stand-in, not a committed
// color.
inline constexpr float kAmbientR = 0.20f;
inline constexpr float kAmbientG = 0.22f;
inline constexpr float kAmbientB = 0.24f;

// Game-side per-frame callbacks the engine invokes. The engine owns the frame
// (window, GL, fixed-step loop, clear, swap); these are where the game does its
// work. Slice stage: the world renders into the pixel target (empty for now)
// and blits up. Systems fill in as the vertical slice is built.

void gameUpdate(Engine& engine, EntityManager& em, double dt);
void gameRenderWorld(Engine& engine, EntityManager& em, float camX, float camY, float alpha);
void gameRenderUI(Engine& engine, EntityManager& em);
