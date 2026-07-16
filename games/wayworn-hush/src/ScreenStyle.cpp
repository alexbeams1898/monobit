#include "ScreenStyle.h"

#include "FontManager.h"

namespace screen_style
{
namespace
{
FontHandle sBody = -1;
FontHandle sLabel = -1;
} // namespace

void init(FontHandle body, FontHandle label)
{
    sBody = body;
    sLabel = label;
}

FontHandle bodyFont()
{
    return sBody;
}

FontHandle labelFont()
{
    return sLabel;
}

void softText(const std::string& s, float x, float y, const Color& c, float alpha)
{
    if (sBody < 0)
        return;
    UIRenderer::drawText(sBody, s, x + 2.0f, y + 2.0f,
                         {kShadow.r, kShadow.g, kShadow.b, kShadow.a * alpha});
    UIRenderer::drawText(sBody, s, x, y, {c.r, c.g, c.b, c.a * alpha});
}

void softTextCentered(const std::string& s, float cx, float y, const Color& c, float alpha)
{
    const auto ts = UIRenderer::measureText(sBody, s);
    softText(s, cx - ts.width * 0.5f, y, c, alpha);
}

float lineH(FontHandle font)
{
    return static_cast<float>(FontManager::lineHeight(font)) + 8.0f;
}

} // namespace screen_style
