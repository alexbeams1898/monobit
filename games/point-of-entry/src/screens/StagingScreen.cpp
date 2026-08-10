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
int sCursor = 0;
bool sConfirmed = false; // a confirm pressed this frame, spent by render

constexpr std::array<const char*, 3> kMenu{"Brew", "Spend points", "Back to work"};

void drawRow(const char* label, float cx, float y, bool hot)
{
    screen_style::entry(label, cx, y, hot, /*enabled=*/true);
}

int pageRows(const EntityManager& em)
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
    sCursor = 0;
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
            sCursor = 0;
            return Action::None;
        }
        return Action::Close;
    }
    if (up)
        --sCursor;
    if (down)
        ++sCursor;
    sConfirmed = confirm;
    return Action::None;
}

Action render(EntityManager& em, const Mouse& mouse, int windowW, int windowH)
{
    screen_style::dim(windowW, windowH);
    const float cx = static_cast<float>(windowW) * 0.5f;
    const float lh = screen_style::lineHeight();
    float y = screen_style::pageHeadingY(windowH);

    const int rows = pageRows(em);
    if (rows > 0)
        sCursor = ((sCursor % rows) + rows) % rows;

    screen_style::headingCentered("STAGING", cx, y, screen_style::kText);
    y += lh * 2.2f;

    Action out = Action::None;
    const float rowH = screen_style::pageRowH();
    const auto rowAt = [&](int i) { return y + lh * 1.4f + rowH * static_cast<float>(i); };
    const auto hover = [&](int i)
    {
        const screen_style::Rect r{cx - lh * 7.0f, rowAt(i) - lh * 0.35f, lh * 14.0f, lh};
        return screen_style::hit(r, mouse.x, mouse.y);
    };

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
            if (hover(i))
            {
                sCursor = i;
                if (mouse.clicked)
                    sConfirmed = true;
            }
            drawRow(kMenu[static_cast<std::size_t>(i)], cx, rowAt(i), i == sCursor);
        }
        if (sConfirmed)
        {
            sConfirmed = false;
            if (sCursor == 0)
                sPage = Page::Brew;
            else if (sCursor == 1)
                sPage = Page::Points;
            else
                out = Action::Close;
            sCursor = 0;
        }
        break;
    }
    case Page::Brew:
    {
        const auto& fills = thermos::fills();
        screen_style::textCentered("what goes in the thermos", cx, y, screen_style::kTextDim);
        for (int i = 0; i < static_cast<int>(fills.size()); ++i)
        {
            if (hover(i))
            {
                sCursor = i;
                if (mouse.clicked)
                    sConfirmed = true;
            }
            const bool held = i == thermos::fillIndex();
            drawRow((fills[static_cast<std::size_t>(i)].name + (held ? "  (in the thermos)" : ""))
                        .c_str(),
                    cx, rowAt(i), i == sCursor);
        }
        if (sConfirmed)
        {
            sConfirmed = false;
            thermos::setFill(em, sCursor); // at the spot, so this refills on the spot
        }
        break;
    }
    case Page::Points:
    {
        const entt::entity p = player::entity();
        if (!em.registry().valid(p) || !em.registry().all_of<Stats>(p))
            break;
        const auto& s = em.registry().get<Stats>(p);
        const bool canBuy = reward::banked(em) >= reward::costOfNext(em);
        screen_style::textCentered("banked " + std::to_string(reward::banked(em)) +
                                       "   next point " + std::to_string(reward::costOfNext(em)),
                                   cx, y, canBuy ? screen_style::kAccent : screen_style::kTextDim);
        const char* names[5] = {"Chemical", "Physical", "Biological", "Endurance", "Inspection"};
        const int values[5] = {s.chemical, s.physical, s.biological, s.endurance, s.inspection};
        for (int i = 0; i < 5; ++i)
        {
            if (hover(i))
            {
                sCursor = i;
                if (mouse.clicked)
                    sConfirmed = true;
            }
            drawRow((std::string{names[i]} + "  " + std::to_string(values[i])).c_str(), cx,
                    rowAt(i), i == sCursor);
        }
        if (sConfirmed)
        {
            sConfirmed = false;
            reward::spend(em, sCursor);
        }
        break;
    }
    }
    return out;
}

} // namespace staging_screen
