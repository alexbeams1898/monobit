#pragma once

// ---------------------------------------------------------------------------
// PixelRenderTarget -- an offscreen framebuffer sized to a fixed low internal
// resolution (the pixel-art resolution), blitted to the window at the largest
// integer scale that fits, centered, with the remainder left as letterbox.
//
// This is the pixel-perfect upscaling path: the world renders once into the
// internal-res target with GL_NEAREST, then a single nearest-neighbor blit
// scales it up by a whole number so every source texel maps to an NxN block of
// window pixels -- no shimmer, no fractional sampling.
//
// Engine-shipped, game-driven (same contract as RenderSystem): the game calls
// init/resize/begin/end/shutdown from its own render + resize callbacks. The
// engine has no opinion about whether a game uses this.
//
// Frame usage (inside the game's setRenderWorld callback):
//     pixelTargetBegin(clearR, clearG, clearB);   // bind FBO, viewport, clear
//     ... draw world at internal resolution ...
//     pixelTargetEnd(windowW, windowH);            // blit upscaled + centered
// UI/text is drawn by the engine AFTER the callback, at native window res, so
// it stays crisp rather than being upscaled with the world.
// ---------------------------------------------------------------------------

namespace engine::gl
{

// The integer-scaled, centered destination rectangle for the blit. Pure data
// so the fit math can be computed and tested without a GL context.
struct BlitRect
{
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    int scale = 1;
};

// Largest integer scale at which internalW x internalH fits inside windowW x
// windowH, then the centered destination rect at that scale. Scale is clamped
// to a minimum of 1 so a window smaller than the internal res still draws
// (cropped by the viewport) rather than vanishing. Pure function -- no GL.
BlitRect computeBlitRect(int internalW, int internalH, int windowW, int windowH);

// Global post-process color grade applied to the whole frame at blit time.
// Defaults are identity (no change) so games that don't set it are unaffected.
// The game owns these values (typically from config) to set the scene's mood.
struct Grade
{
    float saturation = 1.0f; // 1 = unchanged, <1 = muted, 0 = greyscale
    float brightness = 1.0f; // 1 = unchanged, <1 = dimmer
    float tint_r = 1.0f;     // per-channel multiply (cool/warm shift)
    float tint_g = 1.0f;
    float tint_b = 1.0f;
};

// Set the active grade. Applied on every subsequent pixelTargetEnd.
void pixelTargetSetGrade(const Grade& grade);

// Create the FBO + color texture at the internal resolution. Call once after
// the GL context exists.
void pixelTargetInit(int internalW, int internalH);

// Recompute the cached blit rect for a new window size. Call from the game's
// onResize callback (and once after init with the initial window size).
void pixelTargetResize(int windowW, int windowH);

// Bind the FBO, set the viewport to the internal resolution, and clear it to
// the given color (match the engine clear color so letterbox bars and the
// internal image agree). Call at the top of the world-render callback.
void pixelTargetBegin(float r, float g, float b);

// Blit the internal-res target to the default framebuffer at the cached
// integer scale, centered. Call at the end of the world-render callback.
void pixelTargetEnd(int windowW, int windowH);

// Release the FBO + color texture. Call before the GL context is destroyed.
void pixelTargetShutdown();

} // namespace engine::gl
