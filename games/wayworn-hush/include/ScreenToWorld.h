#pragma once

// Convert a window mouse pixel to a world position. Wayworn renders the world at a fixed
// INTERNAL resolution (kInternalWidth x kInternalHeight, zoom 1, camera-centered) into a
// pixel target, which then aspect-fit-blits (centered, letterboxed) to the window. So the
// inverse is two hops: window px -> internal px (undo the blit scale + letterbox offset)
// -> world (add the camera-centered internal offset). Pure math, unit-testable.
namespace screen_to_world
{

struct Result
{
    float x = 0.0f;
    float y = 0.0f;
};

// `blit_*` describe the aspect-fit blit rect of the internal image inside the window
// (from engine::gl::computeBlitRect): blit_x/y = letterbox offset, blit_w/h = the drawn
// size of the internal image. internal_w/h = the render resolution. cam_x/y = the
// world-space camera center. Returns the world position under window pixel (mx,my).
inline Result map(float mx, float my, int blit_x, int blit_y, int blit_w, int blit_h,
                  int internal_w, int internal_h, float cam_x, float cam_y)
{
    // Guard against a zero-size blit (pre-first-resize); map to the camera center.
    if (blit_w <= 0 || blit_h <= 0)
        return {cam_x, cam_y};
    // Window px -> internal px: undo the centering offset, then the blit scale.
    const float internal_px = (mx - static_cast<float>(blit_x)) *
                              (static_cast<float>(internal_w) / static_cast<float>(blit_w));
    const float internal_py = (my - static_cast<float>(blit_y)) *
                              (static_cast<float>(internal_h) / static_cast<float>(blit_h));
    // Internal px -> world: the camera sits at the internal-image center (zoom 1).
    return {cam_x + internal_px - static_cast<float>(internal_w) * 0.5f,
            cam_y + internal_py - static_cast<float>(internal_h) * 0.5f};
}

} // namespace screen_to_world
