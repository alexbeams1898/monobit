#include "screens/SettingsScreen.h"

#include <array>
#include <string>

namespace settings_screen
{
namespace
{
// Two pages: the list, and the one question the list can ask.
enum class Page
{
    List,
    Asking
};
Page sPage = Page::List;
int sCursor = shell_input::kNoChoice;
bool sConfirmed = false; // a confirm pressed this frame, spent by render

// PLACEHOLDER COPY -- the question is Alex's; the two answers under it are stand-ins.
constexpr const char* kForget = "Forget everything";
constexpr const char* kQuestion = "Are you sure you want to forget everything you've learned?";
constexpr std::array<const char*, 2> kAnswers{"No", "Yes"};
constexpr int kYes = 1;

int rowsOn(bool from_title)
{
    if (sPage == Page::Asking)
        return static_cast<int>(kAnswers.size());
    return from_title ? 1 : 0; // nothing else is settable yet
}
} // namespace

void reset()
{
    sPage = Page::List;
    sCursor = shell_input::kNoChoice;
    sConfirmed = false;
}

Action step(bool up, bool down, bool confirm, bool back)
{
    if (back)
    {
        // The question backs out to the list; the list backs out of settings.
        if (sPage == Page::Asking)
        {
            sPage = Page::List;
            sCursor = shell_input::kNoChoice;
            return Action::None;
        }
        return Action::Back;
    }
    // The row count comes from render, which knows which page is up and where it was opened.
    sCursor = shell_input::step(sCursor, sPage == Page::Asking ? 2 : 1, up, down);
    sConfirmed = confirm && sCursor != shell_input::kNoChoice;
    return Action::None;
}

Action render(const shell_input::Mouse& mouse, bool from_title, int windowW, int windowH)
{
    screen_style::dim(windowW, windowH);
    screen_style::panel(screen_style::pagePanelRect(windowW, windowH));

    const float cx = static_cast<float>(windowW) * 0.5f;
    const float lh = screen_style::lineHeight();
    const float rowH = screen_style::pageRowH();
    float y = screen_style::pageHeadingY(windowH);
    screen_style::headingCentered("SETTINGS", cx, y, screen_style::kText);
    y += lh * 2.2f;

    const int rows = rowsOn(from_title);
    if (sCursor >= rows)
        sCursor = shell_input::kNoChoice;
    const auto rowAt = [&](int i) { return y + lh * 1.4f + rowH * static_cast<float>(i); };

    int over = shell_input::kNoChoice;
    for (int i = 0; i < rows; ++i)
        if (screen_style::hit(screen_style::pageRow(cx, rowAt(i)), mouse.x, mouse.y))
            over = i;
    sCursor = shell_input::hover(sCursor, over, mouse.moved);
    if (over != shell_input::kNoChoice && mouse.clicked)
        sConfirmed = true;

    Action out = Action::None;
    if (sPage == Page::Asking)
    {
        screen_style::textCentered(kQuestion, cx, y, screen_style::kText);
        for (int i = 0; i < rows; ++i)
        {
            if (i == kYes)
                screen_style::entryFinal(kAnswers[static_cast<std::size_t>(i)], cx, rowAt(i),
                                         i == sCursor);
            else
                screen_style::entry(kAnswers[static_cast<std::size_t>(i)], cx, rowAt(i),
                                    i == sCursor, /*enabled=*/true);
        }
        if (sConfirmed && sCursor != shell_input::kNoChoice)
        {
            sConfirmed = false;
            out = sCursor == kYes ? Action::Forget : Action::None;
            sPage = Page::List;
            sCursor = shell_input::kNoChoice;
        }
        return out;
    }

    if (rows == 0)
    {
        screen_style::textCentered("nothing to set yet", cx, y, screen_style::kTextDim);
        return Action::None;
    }
    screen_style::entryFinal(kForget, cx, rowAt(0), sCursor == 0);
    if (sConfirmed && sCursor == 0)
    {
        sConfirmed = false;
        sPage = Page::Asking;
        sCursor = shell_input::kNoChoice; // the question opens with neither answer chosen
    }
    return Action::None;
}

} // namespace settings_screen
