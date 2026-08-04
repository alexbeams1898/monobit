#include "Capture.h"

#include "Log.h"

#include <glad/glad.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <algorithm>
#include <vector>

namespace capture
{
namespace
{
bool sRequested = false;
} // namespace

void request()
{
    sRequested = true;
}

void writeIfRequested(int w, int h)
{
    if (!sRequested || w <= 0 || h <= 0)
        return;
    sRequested = false;

    std::vector<unsigned char> px(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());

    // GL's origin is bottom-left and PNG's is top-left, so the rows come out
    // upside down unless they are flipped on the way.
    std::vector<unsigned char> flipped(px.size());
    const std::size_t stride = static_cast<std::size_t>(w) * 4;
    for (int row = 0; row < h; ++row)
        std::copy(
            px.begin() +
                static_cast<std::ptrdiff_t>(static_cast<std::size_t>(h - 1 - row) * stride),
            px.begin() + static_cast<std::ptrdiff_t>(static_cast<std::size_t>(h - row) * stride),
            flipped.begin() + static_cast<std::ptrdiff_t>(static_cast<std::size_t>(row) * stride));

    if (stbi_write_png("frame.png", w, h, 4, flipped.data(), static_cast<int>(stride)) != 0)
        poe::log().info("capture: wrote frame.png ({}x{})", w, h);
    else
        poe::log().error("capture: could not write frame.png");
}

} // namespace capture
