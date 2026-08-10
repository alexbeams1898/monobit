#include "screens/ScreenStyle.h"

#include "FontManager.h"

#include <algorithm>
#include <cmath>

namespace screen_style
{
namespace
{
FontHandle sBody = -1;
FontHandle sHeading = -1;
FontHandle sDisplay = -1;

// How much wider than the text a menu row's hit area is, so the mouse does not have to land
// on a glyph. A fraction of the line height, to scale with the font.
constexpr float kRowPadX = 1.5f;
constexpr float kRowPadY = 0.35f;
} // namespace

void init(FontHandle body, FontHandle heading)
{
    sBody = body;
    sHeading = heading;
}

Color withAlpha(const Color& c, float a)
{
    return Color{c.r, c.g, c.b, a};
}

Color black(float a)
{
    return Color{0.0f, 0.0f, 0.0f, a};
}

int sScale = 1;

int uiScale()
{
    return sScale;
}

float pad(int units)
{
    return static_cast<float>(units * 4 * sScale);
}

void textRight(const std::string& s, float xRight, float y, const Color& c)
{
    text(s, xRight - UIRenderer::measureText(sBody, s).width, y, c);
}

void textInBox(const std::string& s, const Rect& box, const Color& c, bool alignRight)
{
    const auto size = UIRenderer::measureText(sBody, s);
    const float x =
        alignRight ? box.x + box.w - pad(2) - size.width : box.x + (box.w - size.width) * 0.5f;
    const float y = box.y + (box.h - size.height) * 0.5f;
    text(s, x, y, c);
}

float pageHeadingY(int windowH)
{
    return static_cast<float>(windowH) * 0.22f;
}

float pageContentY(int windowH)
{
    return pageHeadingY(windowH) + lineHeight() * 2.4f;
}

float pageRowH()
{
    return lineHeight() * 1.6f;
}

float titleY(int windowH)
{
    return static_cast<float>(windowH) * 0.26f;
}

float titleMenuY(int windowH)
{
    return static_cast<float>(windowH) * 0.62f;
}

void displayCentered(const std::string& s, float cx, float y, const Color& c)
{
    if (sDisplay < 0)
        return;
    const float w = UIRenderer::measureText(sDisplay, s).width;
    UIRenderer::drawText(sDisplay, s, cx - w * 0.5f + 2.0f, y + 2.0f, kShadow);
    UIRenderer::drawText(sDisplay, s, cx - w * 0.5f, y, c);
}

void initFonts(int windowH)
{
    // 16/32 px at 720; whole multiples above. round() rather than floor so 1080p lands on 2x
    // rather than starving at 1x.
    const int k = std::max(1, static_cast<int>(std::lround(static_cast<float>(windowH) / 720.0f)));
    sScale = k;
    const float body = static_cast<float>(16 * k);
    const float heading = static_cast<float>(32 * k);
    init(FontManager::loadFont("assets/fonts/VT323-Regular.ttf", body),
         FontManager::loadFont("assets/fonts/VT323-Regular.ttf", heading));
    sDisplay = FontManager::loadFont("assets/fonts/VT323-Regular.ttf", static_cast<float>(48 * k));
}

float lineHeight()
{
    return sBody < 0 ? 0.0f : FontManager::lineHeight(sBody) + 10.0f;
}

void dim(int windowW, int windowH)
{
    UIRenderer::drawRect(0.0f, 0.0f, static_cast<float>(windowW), static_cast<float>(windowH),
                         kOverlay);
}

int bodyFont()
{
    return sBody;
}

void text(const std::string& s, float x, float y, const Color& c)
{
    if (sBody < 0)
        return;
    UIRenderer::drawText(sBody, s, x + 2.0f, y + 2.0f, kShadow);
    UIRenderer::drawText(sBody, s, x, y, c);
}

void textCentered(const std::string& s, float cx, float y, const Color& c)
{
    if (sBody < 0)
        return;
    text(s, cx - UIRenderer::measureText(sBody, s).width * 0.5f, y, c);
}

void headingCentered(const std::string& s, float cx, float y, const Color& c)
{
    if (sHeading < 0)
        return;
    const float x = cx - UIRenderer::measureText(sHeading, s).width * 0.5f;
    UIRenderer::drawText(sHeading, s, x + 2.0f, y + 2.0f, kShadow);
    UIRenderer::drawText(sHeading, s, x, y, c);
}

Rect entry(const std::string& label, float cx, float y, bool selected, bool enabled)
{
    if (sBody < 0)
        return {};
    const TextSize ts = UIRenderer::measureText(sBody, label);
    const float lh = lineHeight();
    const Rect r{cx - ts.width * 0.5f - lh * kRowPadX, y - lh * kRowPadY,
                 ts.width + lh * kRowPadX * 2.0f, ts.height + lh * kRowPadY * 2.0f};

    // The cursor is a mark in the margin rather than a highlight bar -- a checklist being
    // worked through, which is the register this game wants.
    if (selected && enabled)
        text("-", r.x + lh * 0.4f, y, kAccent);
    text(label, cx - ts.width * 0.5f, y, !enabled ? kTextDim : (selected ? kTextHot : kText));
    return r;
}

bool hit(const Rect& r, float mx, float my)
{
    return r.w > 0.0f && mx >= r.x && mx <= r.x + r.w && my >= r.y && my <= r.y + r.h;
}

} // namespace screen_style
