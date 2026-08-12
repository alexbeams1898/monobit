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

int sCursor = shell_input::kNoChoice;

bool enabled(int i, bool has_save)
{
    return i != kContinue || has_save;
}

void move(int dir, bool has_save)
{
    sCursor = shell_input::stepOver(sCursor, kCount, dir < 0, dir > 0,
                                    [&](int i) { return enabled(i, has_save); });
}
} // namespace

void reset()
{
    sCursor = shell_input::kNoChoice;
}

Action step(bool up, bool down, bool confirm, bool has_save)
{
    // A cursor left on Continue by a previous run, arriving at a title with no save, would sit
    // on an entry that cannot be taken. Land somewhere real first.
    if (sCursor != shell_input::kNoChoice && !enabled(sCursor, has_save))
        move(1, has_save);

    if (up)
        move(-1, has_save);
    if (down)
        move(1, has_save);
    if (confirm && sCursor != shell_input::kNoChoice && enabled(sCursor, has_save))
        return kActions[static_cast<std::size_t>(sCursor)];
    return Action::None;
}

Action render(const shell_input::Mouse& mouse, bool has_save, int windowW, int windowH)
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
        if (!enabled(i, has_save))
            continue;
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
    {
        screen_style::entry(kLabels[static_cast<std::size_t>(i)], cx,
                            y + rowH * static_cast<float>(i), i == sCursor, enabled(i, has_save));
    }
    return committed;
}

} // namespace title_screen
