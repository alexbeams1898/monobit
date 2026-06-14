#pragma once

#include <cstdint>
#include <string>

namespace selva::render
{

// Load a 2D texture from a PNG/JPG path. Returns 0 on failure.
// Path is relative to the working directory. RGBA is requested
// (alpha needed for the branch opacity maps). Mipmaps generated.
std::uint32_t loadTexture2D(const std::string& path);

// Same as loadTexture2D but also writes the source image's pixel
// dimensions to out_w / out_h. Used by the main menu logo so the
// UI knows how to size its draw rect.
std::uint32_t loadTexture2DWithSize(const std::string& path, int& out_w, int& out_h);

void destroyTexture(std::uint32_t tex);

} // namespace selva::render
