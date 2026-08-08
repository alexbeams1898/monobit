#include "screens/TitleScreen.h"

#include <array>

namespace title_screen
{
namespace
{
// The order they are drawn and walked in. Continue sits second because that is where the hand
// expects it, even on a first run when it is not available.
constexpr std::array<const char*, 4> kLabels{"Take the job", "Carry on", "Settings", "Leave"};
constexpr std::array<Action, 4> kActions{Action::NewJob, Action::Continue, Action::Settings,
                                         Action::Quit};
constexpr int kCount = 4;
constexpr int kContinue = 1;

int sCursor = 0;

bool enabled(int i, bool has_save)
{
    return i != kContinue || has_save;
}

// Move the cursor, stepping OVER anything disabled so the hand never lands on a dead entry.
// Bounded by kCount so an all-disabled list cannot spin.
void move(int dir, bool has_save)
{
    for (int guard = 0; guard < kCount; ++guard)
    {
        sCursor = (sCursor + dir + kCount) % kCount;
        if (enabled(sCursor, has_save))
            return;
    }
}
} // namespace

void reset()
{
    sCursor = 0;
}

Action step(bool up, bool down, bool confirm, bool has_save)
{
    // A cursor left on Continue by a previous run, arriving at a title with no save, would sit
    // on an entry that cannot be taken. Land somewhere real first.
    if (!enabled(sCursor, has_save))
        move(1, has_save);

    if (up)
        move(-1, has_save);
    if (down)
        move(1, has_save);
    if (confirm && enabled(sCursor, has_save))
        return kActions[static_cast<std::size_t>(sCursor)];
    return Action::None;
}

Action render(const Mouse& mouse, bool has_save, int windowW, int windowH)
{
    screen_style::dim(windowW, windowH);

    const float cx = static_cast<float>(windowW) * 0.5f;
    const float lh = screen_style::lineHeight();
    float y = static_cast<float>(windowH) * 0.30f;

    screen_style::headingCentered("POINT OF ENTRY", cx, y, screen_style::kText);
    y += lh * 4.0f;

    // Hover resolves BEFORE anything is drawn, so the highlight matches where the mouse is
    // this frame rather than lagging it by one. The rows are a fixed stride, so where each
    // one will land is known without drawing it first.
    Action committed = Action::None;
    const float rowH = lh * 1.6f;
    for (int i = 0; i < kCount; ++i)
    {
        if (!enabled(i, has_save))
            continue;
        const float rowY = y + rowH * static_cast<float>(i);
        const screen_style::Rect r{cx - lh * 6.0f, rowY - lh * 0.35f, lh * 12.0f, lh};
        if (screen_style::hit(r, mouse.x, mouse.y))
        {
            sCursor = i;
            if (mouse.clicked)
                committed = kActions[static_cast<std::size_t>(i)];
        }
    }

    for (int i = 0; i < kCount; ++i)
    {
        screen_style::entry(kLabels[static_cast<std::size_t>(i)], cx,
                            y + rowH * static_cast<float>(i), i == sCursor, enabled(i, has_save));
    }
    return committed;
}

} // namespace title_screen
