#include "ScreenStyle.h"

#include "FontManager.h"
#include "ScreenInput.h"

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

void button(const std::string& label, float x, float y, float w, float h, bool hot, bool enabled)
{
    // A disabled button never reads as hot, however the caller asked -- one place decides
    // what "you can't press this" looks like.
    const bool lit = hot && enabled;
    Color fill = kButtonOff;
    if (enabled)
        fill = lit ? kRowActive : kButtonRest;
    UIRenderer::drawRect(x, y, w, h, fill);

    float alpha = kOffAlpha;
    if (enabled)
        alpha = lit ? 1.0f : kIdleAlpha;
    const auto ts = UIRenderer::measureText(sBody, label);
    softText(label, x + (w - ts.width) * 0.5f, y + (h - ts.height) * 0.5f - 2.0f, kText, alpha);
}

bool buttonHit(float mx, float my, float x, float y, float w, float h)
{
    return engine::ui::pointInRect(mx, my, x, y, w, h);
}

void border(float x, float y, float w, float h, const Color& c, float t)
{
    UIRenderer::drawRect(x, y, w, t, c);
    UIRenderer::drawRect(x, y + h - t, w, t, c);
    UIRenderer::drawRect(x, y, t, h, c);
    UIRenderer::drawRect(x + w - t, y, t, h, c);
}

Inset paperPanel(float x, float y, float w, float h, const Color& accent, float pad_x, float pad_y,
                 float alpha)
{
    UIRenderer::drawRect(x, y, w, h, {kPaper.r, kPaper.g, kPaper.b, kPaper.a * alpha});
    border(x, y, w, h, {kInkBorder.r, kInkBorder.g, kInkBorder.b, 0.85f * alpha});
    const float margin = pad_x * 0.45f;
    UIRenderer::drawRect(x + margin, y + pad_y, 2.0f, h - pad_y * 2.0f,
                         {accent.r, accent.g, accent.b, 0.65f * alpha});
    return {x + pad_x, y + pad_y, w - pad_x * 2.0f, h - pad_y * 2.0f};
}

} // namespace screen_style
