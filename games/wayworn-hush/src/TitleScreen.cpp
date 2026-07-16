#include "TitleScreen.h"

#include "HudCanvas.h"
#include "ScreenInput.h"
#include "ScreenStyle.h"
#include "UIRenderer.h"

#include <algorithm>
#include <string>

namespace title_screen
{
namespace
{
using namespace screen_style;

// The entries, in order. Continue is dropped when there's no save (see entries()),
// so the list the cursor walks is exactly the list that's drawn -- there is no
// hidden/disabled row to reason about.
struct Entry
{
    const char* label;
    Action action;
};

constexpr Entry kWithSave[] = {
    {"Continue", Action::Continue}, {"Begin again", Action::Begin}, {"Leave", Action::Quit}};
constexpr Entry kNoSave[] = {{"Begin", Action::Begin}, {"Leave", Action::Quit}};

// The live entry list + its length for this frame's save state.
const Entry* entries(bool has_save, int& count)
{
    count = has_save ? 3 : 2;
    return has_save ? kWithSave : kNoSave;
}

int sSel = 0;

// Layout, derived once so the draw and the hit-test can't disagree about where a
// row is. Sizes come off the canvas scale so the title reads the same at any
// resolution (the same rule the HUD follows).
struct Layout
{
    float cx;
    float title_y;
    float first_y;
    float row_h;
    float row_w;
};

Layout layout(int windowW, int windowH)
{
    const float s = hud::scale(windowW, windowH);
    const float wh = static_cast<float>(windowH);
    Layout lo;
    lo.cx = static_cast<float>(windowW) * 0.5f;
    lo.title_y = wh * 0.30f;
    lo.first_y = wh * 0.52f;
    lo.row_h = lineH(bodyFont()) * 1.6f;
    lo.row_w = s * 0.26f; // a generous, centered hover band
    return lo;
}

// The row index the mouse is over, or -1.
int rowAt(const Mouse& mouse, const Layout& lo, int count)
{
    for (int i = 0; i < count; ++i)
    {
        const float y = lo.first_y + static_cast<float>(i) * lo.row_h;
        if (engine::ui::pointInRect(mouse.x, mouse.y, lo.cx - lo.row_w * 0.5f, y - lo.row_h * 0.3f,
                                    lo.row_w, lo.row_h))
            return i;
    }
    return -1;
}
} // namespace

void reset()
{
    sSel = 0;
}

Action step(bool up, bool down, bool confirm, bool has_save)
{
    int count = 0;
    const Entry* items = entries(has_save, count);

    if (down)
        sSel = (sSel + 1) % count;
    else if (up)
        sSel = (sSel - 1 + count) % count;
    sSel = std::min(sSel, count - 1); // the list shrinks when a save is wiped

    if (confirm)
        return items[sSel].action;
    return Action::None;
}

Action render(const Mouse& mouse, bool has_save, int windowW, int windowH)
{
    int count = 0;
    const Entry* items = entries(has_save, count);
    sSel = std::min(sSel, count - 1);

    const float ww = static_cast<float>(windowW);
    const float wh = static_cast<float>(windowH);
    const Layout lo = layout(windowW, windowH);

    // No world behind the title -- the overlay IS the screen (see AppState: the
    // region isn't built until the player commits).
    UIRenderer::drawRect(0.0f, 0.0f, ww, wh, kOverlay);

    softTextCentered("Wayworn Hush", lo.cx, lo.title_y, kText);

    // Hovering a row moves the cursor, so the keys and the mouse share one
    // selection rather than fighting over two.
    const int hover = rowAt(mouse, lo, count);
    if (hover >= 0)
        sSel = hover;

    for (int i = 0; i < count; ++i)
    {
        const float y = lo.first_y + static_cast<float>(i) * lo.row_h;
        if (i == sSel)
            UIRenderer::drawRect(lo.cx - lo.row_w * 0.5f, y - lo.row_h * 0.3f, lo.row_w, lo.row_h,
                                 kRowActive);
        softTextCentered(items[i].label, lo.cx, y, kText, i == sSel ? 1.0f : kIdleAlpha);
    }

    if (mouse.clicked && hover >= 0)
        return items[hover].action;
    return Action::None;
}

} // namespace title_screen
