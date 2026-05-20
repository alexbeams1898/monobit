#pragma once

#include <cstdint>
#include <string>

namespace selva::render
{

// Load a 2D texture from a PNG/JPG path. Returns 0 on failure.
// Path is relative to the working directory. RGBA is requested
// (alpha needed for the branch opacity maps). Mipmaps generated.
std::uint32_t loadTexture2D(const std::string& path);

void destroyTexture(std::uint32_t tex);

} // namespace selva::render
