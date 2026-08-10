#include "screens/DeathScreen.h"

#include <array>

namespace death_screen
{
namespace
{
constexpr std::array<const char*, 3> kLabels{"Carry on", "Leave the job", "Quit"};
constexpr std::array<Action, 3> kActions{Action::CarryOn, Action::Leave, Action::Quit};
constexpr int kCount = 3;

int sCursor = 0;
} // namespace

void reset()
{
    sCursor = 0;
}

Action step(bool up, bool down, bool confirm)
{
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
    const float cx = static_cast<float>(windowW) * 0.5f;
    const float lh = screen_style::lineHeight();
    const float y = screen_style::pageContentY(windowH) + lh * 2.0f;

    // No heading. The black and the menu ARE the statement; anything written above them would
    // be the game editorialising about a man who does not.
    Action committed = Action::None;
    const float rowH = screen_style::pageRowH();
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

} // namespace death_screen
