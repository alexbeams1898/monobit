#pragma once

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cmath>

namespace engine::gl
{

// sRGB / linear color-space conversions.
//
// Use case: every authored color (literal in C++, JSON config field,
// shader uniform set from C++) was historically eyeballed against a
// sRGB-displaying monitor. With GL_FRAMEBUFFER_SRGB enabled the GPU
// re-encodes linear -> sRGB on framebuffer write, so any color the
// shader receives must be in LINEAR space for the math + display
// chain to produce the same visual result.
//
// Two conventions:
//   - linearize(c)  : input is sRGB-encoded byte/float (8-bit JPEG
//                     color, an authored "looks right on screen"
//                     value, a JSON [r,g,b] triple). Output is
//                     linear-space, suitable for shader uniforms +
//                     lighting math.
//   - srgbEncode(c) : input is linear, output is sRGB-encoded. Used
//                     only when CPU code needs to round-trip a color
//                     into a display-referred byte (e.g. screenshot
//                     readback, ImGui color picker writing back to
//                     storage). The framebuffer handles this for us
//                     at draw time; you almost never call this in
//                     normal flow.
//
// The exact sRGB transfer curve has a small linear segment near
// black; we use the IEC 61966-2-1 piecewise definition for
// correctness. The simple `pow(c, 2.2)` shortcut is visually close
// enough at midtones but wrong at the dark end; this lib is the
// place to be precise.

inline float linearizeChannel(float srgb)
{
    if (srgb <= 0.04045f)
        return srgb / 12.92f;
    return std::pow((srgb + 0.055f) / 1.055f, 2.4f);
}

inline float srgbEncodeChannel(float linear)
{
    if (linear <= 0.0031308f)
        return linear * 12.92f;
    return 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
}

inline glm::vec3 linearize(const glm::vec3& srgb)
{
    return {linearizeChannel(srgb.x), linearizeChannel(srgb.y), linearizeChannel(srgb.z)};
}

inline glm::vec4 linearize(const glm::vec4& srgba)
{
    // Alpha is NOT sRGB-encoded; it stays linear.
    return {linearizeChannel(srgba.x), linearizeChannel(srgba.y), linearizeChannel(srgba.z),
            srgba.w};
}

inline glm::vec3 srgbEncode(const glm::vec3& linear)
{
    return {srgbEncodeChannel(linear.x), srgbEncodeChannel(linear.y), srgbEncodeChannel(linear.z)};
}

inline glm::vec4 srgbEncode(const glm::vec4& linear)
{
    return {srgbEncodeChannel(linear.x), srgbEncodeChannel(linear.y), srgbEncodeChannel(linear.z),
            linear.w};
}

} // namespace engine::gl
