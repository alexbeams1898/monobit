#include "screens/TitleScreen.h"

#include <array>

namespace title_screen
{
namespace
{
// The order they are drawn and walked in. One way in, so there is no entry here that can be
// unavailable and none that can destroy anything.
constexpr std::array<const char*, 3> kLabels{"Take the job", "Settings", "Quit"};
constexpr std::array<Action, 3> kActions{Action::Work, Action::Settings, Action::Quit};
constexpr int kCount = 3;

int sCursor = shell_input::kNoChoice;
} // namespace

void reset()
{
    sCursor = shell_input::kNoChoice;
}

Action step(bool up, bool down, bool confirm)
{
    sCursor = shell_input::step(sCursor, kCount, up, down);
    if (confirm && sCursor != shell_input::kNoChoice)
        return kActions[static_cast<std::size_t>(sCursor)];
    return Action::None;
}

Action render(const shell_input::Mouse& mouse, int windowW, int windowH)
{
    screen_style::dim(windowW, windowH);

    const float cx = static_cast<float>(windowW) * 0.5f;
    // The title's own shape: the name large in the upper third, the menu low.
    screen_style::displayCentered("POINT OF ENTRY", cx, screen_style::titleY(windowH),
                                  screen_style::kText);
    float y = screen_style::titleMenuY(windowH);

    // Hover resolves BEFORE anything is drawn, so the highlight matches where the mouse is
    // this frame rather than lagging it by one. The rows are a fixed stride, so where each
    // one will land is known without drawing it first.
    Action committed = Action::None;
    const float rowH = screen_style::pageRowH();
    int over = shell_input::kNoChoice;
    for (int i = 0; i < kCount; ++i)
    {
        const float rowY = y + rowH * static_cast<float>(i);
        if (screen_style::hit(screen_style::pageRow(cx, rowY), mouse.x, mouse.y))
        {
            over = i;
            if (mouse.clicked)
                committed = kActions[static_cast<std::size_t>(i)];
        }
    }
    sCursor = shell_input::hover(sCursor, over, mouse.moved);

    for (int i = 0; i < kCount; ++i)
        screen_style::entry(kLabels[static_cast<std::size_t>(i)], cx,
                            y + rowH * static_cast<float>(i), i == sCursor, /*enabled=*/true);
    return committed;
}

} // namespace title_screen
