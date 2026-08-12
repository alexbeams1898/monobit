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

// How far above a row's baseline its band starts, and how tall the band is -- fractions of the
// line height, so the strike area scales with the font.
constexpr float kRowPadY = 0.35f;
constexpr float kRowH = 1.3f;

// How far a lit link's marks stand off its words, as a fraction of the line height.
constexpr float kMarkGap = 0.45f;

// THE COLUMN's width in spacing units: a page's proportion of the window it was drawn for,
// wide enough to hold a paragraph of an entry without the words running to a thread, with
// margin enough left over that the frozen world still shows around it.
constexpr int kColumnUnits = 192;

// THE PAGE'S FRAME, in spacing units: how far the page sits in from the window, and its own
// padding inside that. One number each, spent on every side -- the page is centred and its
// field is evenly inset by construction, rather than by four anchors happening to agree. The
// heading sits at the top of the padded field, which is what puts it where a heading goes.
constexpr int kPageMarginUnits = 24;
constexpr int kPagePadUnits = 12;
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

void panel(const Rect& r)
{
    UIRenderer::drawRect(r.x - 1.0f, r.y - 1.0f, r.w + 2.0f, r.h + 2.0f, kPanelEdge);
    UIRenderer::drawRect(r.x, r.y, r.w, r.h, kPanel);
}

void paperPanel(const Rect& r)
{
    UIRenderer::drawRect(r.x - 1.0f, r.y - 1.0f, r.w + 2.0f, r.h + 2.0f, kInk);
    UIRenderer::drawRect(r.x, r.y, r.w, r.h, kPaper);
    // The margin rule a working notebook carries, inside the left edge.
    UIRenderer::drawRect(r.x + pad(3), r.y, 1.0f, r.h, kAccent);
}

float pageHeadingY(int)
{
    // The top of the padded field: the heading belongs to the page, not to a fraction of the
    // window, and not to the frame's own edge.
    return pad(kPageMarginUnits) + pad(kPagePadUnits);
}

float pageContentY(int windowH)
{
    return pageHeadingY(windowH) + lineHeight() * 2.4f;
}

float pageRowH()
{
    return lineHeight() * 1.6f;
}

float pageColumnW()
{
    return pad(kColumnUnits);
}

float pageColumnLeft(float cx)
{
    return cx - pageColumnW() * 0.5f;
}

Rect bandRow(const Rect& band, float y)
{
    const float lh = lineHeight();
    return Rect{band.x, y - lh * kRowPadY, band.w, lh * kRowH};
}

Rect pageRow(float cx, float y)
{
    return bandRow(Rect{pageColumnLeft(cx), 0.0f, pageColumnW(), 0.0f}, y);
}

Rect pageTab(float cx, float y, int index, int count)
{
    const float w = pageColumnW() / static_cast<float>(std::max(1, count));
    const float lh = lineHeight();
    return Rect{pageColumnLeft(cx) + w * static_cast<float>(index), y - lh * kRowPadY, w,
                lh * kRowH};
}

float pageStop(const Rect& row, float t)
{
    return row.x + row.w * t;
}

// The marks a lit link wears, around a label already drawn at x. Shared, so a
// red link and a plain one are recognisably the same thing.
void marks(const std::string& s, float x, float y)
{
    // The marks sit OUTSIDE the label's own width, so lighting a row never nudges its words.
    const float gap = lineHeight() * kMarkGap;
    const float open = UIRenderer::measureText(sBody, "[").width;
    text("[", x - gap - open, y, kAccent);
    text("]", x + UIRenderer::measureText(sBody, s).width + gap, y, kAccent);
}

void link(const std::string& s, float x, float y, LinkState state)
{
    if (sBody < 0)
        return;
    text(s, x, y,
         state == LinkState::Faint ? kTextDim : (state == LinkState::Hot ? kTextHot : kText));
    if (state == LinkState::Hot)
        marks(s, x, y);
}

void linkCentered(const std::string& s, float cx, float y, LinkState state)
{
    if (sBody < 0)
        return;
    link(s, cx - UIRenderer::measureText(sBody, s).width * 0.5f, y, state);
}

Rect pagePanelRect(int windowW, int windowH)
{
    const float margin = pad(kPageMarginUnits);
    const float inner = pad(kPagePadUnits);
    const float cx = static_cast<float>(windowW) * 0.5f;
    return Rect{pageColumnLeft(cx) - inner, margin, pageColumnW() + inner * 2.0f,
                static_cast<float>(windowH) - margin * 2.0f};
}

float pageBottom(int windowH)
{
    return static_cast<float>(windowH) - pad(kPageMarginUnits) - pad(kPagePadUnits);
}

Rect pageBand(int windowW, float top, float bottom)
{
    const float cx = static_cast<float>(windowW) * 0.5f;
    return Rect{pageColumnLeft(cx), top, pageColumnW(), bottom - top};
}

Split pageSplit(int windowW, float top, float bottom, float share)
{
    const Rect all = pageBand(windowW, top, bottom);
    const float gutter = pad(4);
    const float leftW = (all.w - gutter) * share;
    return Split{Rect{all.x, top, leftW, all.h},
                 Rect{all.x + leftW + gutter, top, all.w - leftW - gutter, all.h}};
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

void inkText(const std::string& s, float x, float y, const Color& c)
{
    if (sBody < 0)
        return;
    UIRenderer::drawText(sBody, s, x, y, c);
}

void inkTextCentered(const std::string& s, float cx, float y, const Color& c)
{
    if (sBody < 0)
        return;
    inkText(s, cx - UIRenderer::measureText(sBody, s).width * 0.5f, y, c);
}

void headingCentered(const std::string& s, float cx, float y, const Color& c)
{
    if (sHeading < 0)
        return;
    const float x = cx - UIRenderer::measureText(sHeading, s).width * 0.5f;
    UIRenderer::drawText(sHeading, s, x + 2.0f, y + 2.0f, kShadow);
    UIRenderer::drawText(sHeading, s, x, y, c);
}

Rect entryFinal(const std::string& label, float cx, float y, bool selected)
{
    if (sBody < 0)
        return {};
    // Red, which nothing else on a menu wears: the one row that cannot be taken
    // back should not read like the rows that can.
    const float x = cx - UIRenderer::measureText(sBody, label).width * 0.5f;
    text(label, x, y, selected ? kAccent : withAlpha(kAccent, 0.65f));
    if (selected)
        marks(label, x, y);
    return pageRow(cx, y);
}

Rect entry(const std::string& label, float cx, float y, bool selected, bool enabled)
{
    if (sBody < 0)
        return {};
    linkCentered(label, cx, y,
                 !enabled ? LinkState::Faint : (selected ? LinkState::Hot : LinkState::Idle));
    return pageRow(cx, y);
}

bool hit(const Rect& r, float mx, float my)
{
    return r.w > 0.0f && mx >= r.x && mx <= r.x + r.w && my >= r.y && my <= r.y + r.h;
}

} // namespace screen_style
