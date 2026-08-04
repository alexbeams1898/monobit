#include "PauseScreen.h"

#include <array>

namespace pause_screen
{
namespace
{
// Leaving the job puts you back at the title; quitting closes the game. Both are here because
// a player who is done is done, and making them pass through the title to close a window is a
// small rudeness that costs nothing to avoid.
constexpr std::array<const char*, 4> kLabels{"Resume", "Settings", "Leave the job", "Quit"};
constexpr std::array<Action, 4> kActions{Action::Resume, Action::Settings, Action::Leave,
                                         Action::Quit};
constexpr int kCount = 4;

int sCursor = 0;
} // namespace

void reset()
{
    sCursor = 0;
}

Action step(bool up, bool down, bool confirm, bool back)
{
    if (back)
        return Action::Resume; // Esc always gets you out, from anywhere on the page
    if (up)
        sCursor = (sCursor - 1 + kCount) % kCount;
    if (down)
        sCursor = (sCursor + 1) % kCount;
    if (confirm)
        return kActions[static_cast<std::size_t>(sCursor)];
    return Action::None;
}

Action render(const Mouse& mouse, int windowW, int windowH)
{
    screen_style::dim(windowW, windowH);

    const float cx = static_cast<float>(windowW) * 0.5f;
    const float lh = screen_style::lineHeight();
    float y = static_cast<float>(windowH) * 0.28f;

    screen_style::headingCentered("PAUSED", cx, y, screen_style::kText);
    y += lh * 2.0f;

    // Hover before the draw, so the highlight matches the mouse this frame.
    Action committed = Action::None;
    const float rowH = lh * 1.6f;
    for (int i = 0; i < kCount; ++i)
    {
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
        screen_style::entry(kLabels[static_cast<std::size_t>(i)], cx,
                            y + rowH * static_cast<float>(i), i == sCursor, /*enabled=*/true);
    return committed;
}

} // namespace pause_screen
