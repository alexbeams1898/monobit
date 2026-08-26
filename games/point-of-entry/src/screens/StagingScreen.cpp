#include "screens/StagingScreen.h"

#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "ecs/GameComponents.h"
#include "systems/PlayerSystem.h"
#include "systems/RewardSystem.h"
#include "systems/ThermosSystem.h"

#include <array>
#include <string>

#include <entt/entt.hpp>

namespace staging_screen
{
namespace
{
// Three pages: the menu, and one per transaction.
enum class Page
{
    Menu,
    Brew,
    Points
};
Page sPage = Page::Menu;
int sCursor = shell_input::kNoChoice;
bool sConfirmed = false; // a confirm pressed this frame, spent by render
int sRows = 0;           // rows on the page as last drawn, for the keyboard to walk

constexpr std::array<const char*, 3> kMenu{"Brew", "Spend points", "Back to work"};

void drawRow(const char* label, float cx, float y, bool hot)
{
    screen_style::entry(label, cx, y, hot, /*enabled=*/true);
}

int pageRows(const EntityManager& /*em*/)
{
    switch (sPage)
    {
    case Page::Brew:
        return static_cast<int>(thermos::fills().size());
    case Page::Points:
        return 5;
    case Page::Menu:
    default:
        return static_cast<int>(kMenu.size());
    }
}

} // namespace

void reset()
{
    sPage = Page::Menu;
    sCursor = shell_input::kNoChoice;
    sConfirmed = false;
}

Action step(bool up, bool down, bool confirm, bool back)
{
    if (back)
    {
        // A page backs out to the menu; the menu backs out to work.
        if (sPage != Page::Menu)
        {
            sPage = Page::Menu;
            sCursor = shell_input::kNoChoice;
            return Action::None;
        }
        return Action::Close;
    }
    sCursor = shell_input::step(sCursor, sRows, up, down);
    sConfirmed = confirm && sCursor != shell_input::kNoChoice;
    return Action::None;
}

// WHERE A PAGE'S ROWS ARE, and which one the mouse is over. Passed to a page rather than
// recomputed by it, so every page lays out identically and a row is hit-tested against exactly
// the rect it was drawn into.
struct Sheet
{
    float cx = 0.0f;
    float y = 0.0f;
    float lh = 0.0f;
    float row_h = 0.0f;
    int over = shell_input::kNoChoice;

    float rowAt(int i) const
    {
        return y + lh * 1.4f + row_h * static_cast<float>(i);
    }
    bool hover(int i) const
    {
        return i == over;
    }
};

// WHAT GOES IN THE THERMOS. Its own function because a page is a page: the switch below is the
// shape of the screen, and reading one page should not mean reading past the others.
void drawBrew(EntityManager& em, const Sheet& sheet, const shell_input::Mouse& mouse)
{
    const float cx = sheet.cx;
    const float y = sheet.y;
    const auto& fills = thermos::fills();
    screen_style::textCentered("what goes in the thermos", cx, y, screen_style::kTextDim);
    for (int i = 0; i < static_cast<int>(fills.size()); ++i)
    {
        if (sheet.hover(i) && mouse.clicked)
            sConfirmed = true;
        const bool held = i == thermos::fillIndex();
        drawRow(
            (fills[static_cast<std::size_t>(i)].name + (held ? "  (in the thermos)" : "")).c_str(),
            cx, sheet.rowAt(i), i == sCursor);
    }
    if (sConfirmed && sCursor != shell_input::kNoChoice)
    {
        sConfirmed = false;
        thermos::setFill(em, sCursor); // at the spot, so this refills on the spot
    }
}

// THE SHEET, and what the next point costs. Same reason.
void drawPoints(EntityManager& em, const Sheet& sheet, const shell_input::Mouse& mouse)
{
    const float cx = sheet.cx;
    const float y = sheet.y;
    const entt::entity p = player::entity();
    if (!em.registry().valid(p) || !em.registry().all_of<Stats>(p))
        return;
    const auto& s = em.registry().get<Stats>(p);
    const bool canBuy = reward::banked(em) >= reward::costOfNext(em);
    screen_style::textCentered("banked " + std::to_string(reward::banked(em)) + "   next point " +
                                   std::to_string(reward::costOfNext(em)),
                               cx, y, canBuy ? screen_style::kAccent : screen_style::kTextDim);
    const char* names[5] = {"Chemical", "Physical", "Biological", "Endurance", "Inspection"};
    const int values[5] = {s.chemical, s.physical, s.biological, s.endurance, s.inspection};
    for (int i = 0; i < 5; ++i)
    {
        if (sheet.hover(i) && mouse.clicked)
            sConfirmed = true;
        drawRow((std::string{names[i]} + "  " + std::to_string(values[i])).c_str(), cx,
                sheet.rowAt(i), i == sCursor);
    }
    if (sConfirmed && sCursor != shell_input::kNoChoice)
    {
        sConfirmed = false;
        reward::spend(em, sCursor);
    }
}

Action render(EntityManager& em, const shell_input::Mouse& mouse, int windowW, int windowH)
{
    screen_style::dim(windowW, windowH);
    const float cx = static_cast<float>(windowW) * 0.5f;
    const float lh = screen_style::lineHeight();
    float y = screen_style::pageHeadingY(windowH);

    // The row count the NEXT keypress will walk: step() needs it, and only render knows the
    // page's contents.
    sRows = pageRows(em);
    if (sCursor >= sRows)
        sCursor = shell_input::kNoChoice;

    screen_style::headingCentered("STAGING", cx, y, screen_style::kText);
    y += lh * 2.2f;

    Action out = Action::None;
    Sheet sheet{cx, y, lh, screen_style::pageRowH(), shell_input::kNoChoice};
    for (int i = 0; i < sRows; ++i)
        if (screen_style::hit(screen_style::pageRow(cx, sheet.rowAt(i)), mouse.x, mouse.y))
            sheet.over = i;
    sCursor = shell_input::hover(sCursor, sheet.over, mouse.moved);
    const auto rowAt = [&](int i) { return sheet.rowAt(i); };
    const auto hover = [&](int i) { return sheet.hover(i); };

    switch (sPage)
    {
    case Page::Menu:
    {
        screen_style::textCentered("thermos  " + std::to_string(thermos::sipsLeft()) + "/" +
                                       std::to_string(thermos::sipsMax()) + "   banked " +
                                       std::to_string(reward::banked(em)),
                                   cx, y, screen_style::kTextDim);
        for (int i = 0; i < static_cast<int>(kMenu.size()); ++i)
        {
            if (hover(i) && mouse.clicked)
                sConfirmed = true;
            drawRow(kMenu[static_cast<std::size_t>(i)], cx, rowAt(i), i == sCursor);
        }
        if (sConfirmed && sCursor != shell_input::kNoChoice)
        {
            sConfirmed = false;
            if (sCursor == 0)
                sPage = Page::Brew;
            else if (sCursor == 1)
                sPage = Page::Points;
            else
                out = Action::Close;
            sCursor = shell_input::kNoChoice;
        }
        break;
    }
    case Page::Brew:
        drawBrew(em, sheet, mouse);
        break;
    case Page::Points:
        drawPoints(em, sheet, mouse);
        break;
    }
    return out;
}

} // namespace staging_screen
