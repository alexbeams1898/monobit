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

// The entries, in order. The list never changes shape: Load Game is DISABLED when nobody
// has walked, not removed. A menu whose rows move under the hand -- because a delete
// elsewhere changed how many there are -- is how you press the wrong thing; the row stays
// put and stops being pressable.
struct Entry
{
    const char* label;
    Action action;
};

constexpr Entry kEntries[] = {{"New Game", Action::NewGame},
                              {"Load Game", Action::LoadGame},
                              {"Settings", Action::Settings},
                              {"Quit", Action::Quit}};
constexpr int kEntryCount = 4;

// Whether an entry can be chosen right now. Load Game needs someone to load.
bool enabled(const Entry& e, bool has_save)
{
    return e.action != Action::LoadGame || has_save;
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
    lo.title_y = wh * 0.28f;
    lo.first_y = wh * 0.48f;
    lo.row_h = lineH(bodyFont()) * 1.9f; // button height + the gap under it
    lo.row_w = s * 0.20f;
    return lo;
}

// A button's rect. One definition, so the drawn rect and the pressed rect are the same.
float btnX(const Layout& lo)
{
    return lo.cx - lo.row_w * 0.5f;
}
float btnY(const Layout& lo, int i)
{
    return lo.first_y + static_cast<float>(i) * lo.row_h;
}
float btnH(const Layout& lo)
{
    return lo.row_h * 0.72f;
}

// The button index the mouse is over, or -1.
int rowAt(const Mouse& mouse, const Layout& lo, int count)
{
    for (int i = 0; i < count; ++i)
        if (buttonHit(mouse.x, mouse.y, btnX(lo), btnY(lo, i), lo.row_w, btnH(lo)))
            return i;
    return -1;
}
} // namespace

void reset()
{
    sSel = 0;
}

Action step(bool up, bool down, bool confirm, bool has_save)
{
    if (down || up)
    {
        // Step OVER a disabled row rather than landing on it: W/S should never park the
        // cursor somewhere Space does nothing. Bounded by the entry count, so a list that
        // was somehow all-disabled leaves the cursor where it was instead of spinning.
        const int dir = down ? 1 : -1;
        for (int n = 0; n < kEntryCount; ++n)
        {
            sSel = (sSel + dir + kEntryCount) % kEntryCount;
            if (enabled(kEntries[sSel], has_save))
                break;
        }
    }

    if (confirm && enabled(kEntries[sSel], has_save))
        return kEntries[sSel].action;
    return Action::None;
}

Action render(const Mouse& mouse, bool has_save, int windowW, int windowH)
{
    // The cursor can be left on Load Game by a walk that was then forgotten (the last
    // pilgrim deleted, sending us back here). Move it off rather than leave it resting on a
    // row that no longer does anything.
    if (!enabled(kEntries[sSel], has_save))
        sSel = 0;

    const float ww = static_cast<float>(windowW);
    const float wh = static_cast<float>(windowH);
    const Layout lo = layout(windowW, windowH);

    // No world behind the title -- the overlay IS the screen (see AppState: the
    // region isn't built until the player commits).
    UIRenderer::drawRect(0.0f, 0.0f, ww, wh, kOverlay);

    softTextCentered("Wayworn Hush", lo.cx, lo.title_y, kText);

    // Hovering a row moves the cursor, so the keys and the mouse share one selection rather
    // than fighting over two. A disabled row doesn't take the cursor and doesn't press.
    const int hover = rowAt(mouse, lo, kEntryCount);
    const bool hotRow = hover >= 0 && enabled(kEntries[hover], has_save);
    if (hotRow)
        sSel = hover;

    for (int i = 0; i < kEntryCount; ++i)
        button(kEntries[i].label, btnX(lo), btnY(lo, i), lo.row_w, btnH(lo), i == sSel,
               enabled(kEntries[i], has_save));

    if (mouse.clicked && hotRow)
        return kEntries[hover].action;
    return Action::None;
}

} // namespace title_screen
