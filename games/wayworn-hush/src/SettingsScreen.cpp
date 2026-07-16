#include "SettingsScreen.h"

#include "HudCanvas.h"
#include "ScreenInput.h"
#include "ScreenStyle.h"
#include "UIRenderer.h"

#include <string>
#include <vector>

namespace settings_screen
{
namespace
{
using namespace screen_style;

// One row: what it's called, how its current value reads, and how to change it. Holding the
// behavior as functions keeps step() and render() reading the SAME table -- a row's meaning
// lives in one place rather than in a switch in each.
struct Row
{
    const char* label;
    std::string (*value)(const settings::Settings&);
    void (*advance)(settings::Settings&, int dir); // dir: +1 / -1
};

// --- the settings themselves ---

std::string hudModeValue(const settings::Settings& s)
{
    switch (s.hud.visibility)
    {
    case hud::Visibility::On:
        return "Always";
    case hud::Visibility::Off:
        return "Never";
    case hud::Visibility::Auto:
        break;
    }
    return "When needed";
}

void hudModeAdvance(settings::Settings& s, int dir)
{
    // Auto -> On -> Off, wrapping. Cycles in the order the labels read.
    constexpr hud::Visibility kOrder[] = {hud::Visibility::Auto, hud::Visibility::On,
                                          hud::Visibility::Off};
    constexpr int n = 3;
    int i = 0;
    for (int k = 0; k < n; ++k)
        if (kOrder[k] == s.hud.visibility)
            i = k;
    s.hud.visibility = kOrder[(i + dir + n) % n];
}

std::string yesNo(bool b)
{
    return b ? "Yes" : "No";
}

std::string timeValue(const settings::Settings& s)
{
    return yesNo(s.hud.show_time);
}
std::string stanceValue(const settings::Settings& s)
{
    return yesNo(s.hud.show_stance);
}
// A toggle ignores direction -- there are only two values, so either way is the other one.
void timeAdvance(settings::Settings& s, int)
{
    s.hud.show_time = !s.hud.show_time;
}
void stanceAdvance(settings::Settings& s, int)
{
    s.hud.show_stance = !s.hud.show_stance;
}
// Status only. The game's speech (readings, thoughts, toasts) is content, not a HUD
// widget, and has no row here -- see docs/design/HUD.md.
constexpr Row kHudRows[] = {
    {"Show the HUD", hudModeValue, hudModeAdvance},
    {"The time", timeValue, timeAdvance},
    {"The stance badge", stanceValue, stanceAdvance},
};
constexpr int kHudRowCount = 3;

// Which page is showing. The top page lists CATEGORIES; a category page lists its settings.
// Settings grows by adding a category here, not by lengthening one list -- audio and display
// are coming, and a single flat page of everything is how a settings screen becomes a wall.
enum class Page
{
    Top,
    Hud
};

// The top page's entries: a category each, opened with confirm.
struct Category
{
    const char* label;
    Page page;
};
constexpr Category kCategories[] = {{"HUD", Page::Hud}};
constexpr int kCategoryCount = 1;

Page sPage = Page::Top;
int sSel = 0;

// How many rows the showing page has.
int rowCount()
{
    return sPage == Page::Top ? kCategoryCount : kHudRowCount;
}

// What the showing page's row `i` is called.
const char* rowLabel(int i)
{
    return sPage == Page::Top ? kCategories[i].label : kHudRows[i].label;
}

// Layout, derived once so the draw and the hit-test can't disagree about where a row is.
// Mirrors the title's shape: sizes off the canvas scale, so it reads the same at any
// resolution.
struct Layout
{
    float cx;
    float title_y;
    float first_y;
    float row_h;
    float row_w;
    float back_y;
};

Layout layout(int windowW, int windowH)
{
    const float s = hud::scale(windowW, windowH);
    const float wh = static_cast<float>(windowH);
    Layout lo;
    lo.cx = static_cast<float>(windowW) * 0.5f;
    lo.title_y = wh * 0.22f;
    lo.first_y = wh * 0.36f;
    lo.row_h = lineH(bodyFont()) * 1.7f;
    lo.row_w = s * 0.46f; // wide: a row is a label on the left, its value on the right
    lo.back_y = lo.first_y + static_cast<float>(rowCount()) * lo.row_h + lo.row_h * 0.8f;
    return lo;
}

float rowX(const Layout& lo)
{
    return lo.cx - lo.row_w * 0.5f;
}
float rowY(const Layout& lo, int i)
{
    return lo.first_y + static_cast<float>(i) * lo.row_h;
}
float rowH(const Layout& lo)
{
    return lo.row_h * 0.78f;
}

// The Back button's rect -- narrower than a settings row, like the title's entries.
float backW(const Layout& lo)
{
    return lo.row_w * 0.42f;
}
float backX(const Layout& lo)
{
    return lo.cx - backW(lo) * 0.5f;
}

// The row the mouse is over, or -1.
int rowAt(const Mouse& mouse, const Layout& lo)
{
    for (int i = 0; i < rowCount(); ++i)
        if (buttonHit(mouse.x, mouse.y, rowX(lo), rowY(lo, i), lo.row_w, rowH(lo)))
            return i;
    return -1;
}

// One row: label left, its value right (or a "goes deeper" mark, for a category), in a soft
// trough that lifts under the cursor. Not screen_style::button -- a button is one thing you
// press; this is a thing with a value.
void drawRow(const std::string& label, const std::string& value, const Layout& lo, int i,
             bool active)
{
    const float x = rowX(lo);
    const float y = rowY(lo, i);
    const float h = rowH(lo);
    UIRenderer::drawRect(x, y, lo.row_w, h, active ? kRowActive : kButtonRest);

    const float pad = lo.row_w * 0.04f;
    const auto ts = UIRenderer::measureText(bodyFont(), label);
    const float textY = y + (h - ts.height) * 0.5f;
    softText(label, x + pad, textY, kText, active ? 1.0f : kIdleAlpha);

    const float vw = UIRenderer::measureText(bodyFont(), value).width;
    softText(value, x + lo.row_w - pad - vw, textY, kText, active ? 1.0f : kIdleAlpha);
}
} // namespace

void reset()
{
    sPage = Page::Top;
    sSel = 0;
}

Action step(settings::Settings& s, bool up, bool down, bool left, bool right, bool confirm,
            bool back)
{
    if (back)
    {
        // Back steps OUT of a category first; only the top page leaves the screen.
        if (sPage == Page::Top)
            return Action::Back;
        sPage = Page::Top;
        sSel = 0;
        return Action::None;
    }

    const int n = rowCount();
    if (down)
        sSel = (sSel + 1) % n;
    else if (up)
        sSel = (sSel - 1 + n) % n;

    if (sPage == Page::Top)
    {
        if (confirm)
        {
            sPage = kCategories[sSel].page;
            sSel = 0;
        }
        return Action::None; // a category has no value to change
    }

    if (left || right)
        kHudRows[sSel].advance(s, right ? 1 : -1);
    return Action::None;
}

Action render(settings::Settings& s, const Mouse& mouse, int windowW, int windowH)
{
    const float ww = static_cast<float>(windowW);
    const float wh = static_cast<float>(windowH);
    const Layout lo = layout(windowW, windowH);

    // Sits over whatever is behind: nothing (from the title) or the frozen world (from a
    // walk). The same darkening the pause page uses, so the two read as one surface.
    UIRenderer::drawRect(0.0f, 0.0f, ww, wh, kOverlay);
    // The heading says where you are: "Settings", or the category you stepped into.
    softTextCentered(sPage == Page::Top ? "Settings" : "Settings  .  HUD", lo.cx, lo.title_y,
                     kText);

    // Hovering moves the cursor, so keys and mouse share one selection.
    const int hover = rowAt(mouse, lo);
    if (hover >= 0)
        sSel = hover;

    for (int i = 0; i < rowCount(); ++i)
    {
        // A category shows a chevron where a setting shows its value -- it goes somewhere
        // rather than being something.
        const std::string value = sPage == Page::Top ? std::string{">"} : kHudRows[i].value(s);
        drawRow(rowLabel(i), value, lo, i, i == sSel);
    }

    const bool backHot = buttonHit(mouse.x, mouse.y, backX(lo), lo.back_y, backW(lo), rowH(lo));
    button("Back", backX(lo), lo.back_y, backW(lo), rowH(lo), backHot);

    if (mouse.clicked)
    {
        if (backHot)
        {
            if (sPage == Page::Top)
                return Action::Back;
            sPage = Page::Top; // out of the category first -- same rule the key follows
            sSel = 0;
            return Action::None;
        }
        if (hover >= 0)
        {
            if (sPage == Page::Top)
            {
                sPage = kCategories[hover].page; // clicking a category opens it
                sSel = 0;
            }
            else
                kHudRows[hover].advance(s, 1); // clicking a setting is pressing right on it
        }
    }
    return Action::None;
}

} // namespace settings_screen
