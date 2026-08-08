#include "screens/PauseScreen.h"

#include "ecs/BalanceConfig.h"
#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "systems/CombatSystem.h"
#include "systems/PlayerSystem.h"
#include "systems/RewardSystem.h"

#include <array>
#include <string>

#include <entt/entt.hpp>

namespace pause_screen
{
namespace
{
// The tabs. Sheet first: pausing to look at yourself is the common case; the machine's
// business is one keypress right.
constexpr std::array<const char*, 2> kTabs{"Sheet", "System"};
constexpr int kTabCount = 2;

// No Resume row: Esc IS resume, from anywhere on the screen, and a menu entry duplicating the
// key that opened the menu is noise. Leaving puts you back at the title; quitting closes the
// game -- both here because making a player who is done pass through the title to close a
// window is a small rudeness that costs nothing to avoid.
constexpr std::array<const char*, 3> kLabels{"Settings", "Leave the job", "Quit"};
constexpr std::array<Action, 3> kActions{Action::Settings, Action::Leave, Action::Quit};
constexpr int kCount = 3;

int sTab = 0;
int sCursor = 0;
int sStatCursor = 0;          // which sheet row, while there are points to spend
bool sSpendRequested = false; // confirm pressed on the sheet this frame

// One stat row: name left, value right of centre. A document while there is nothing to spend;
// the moment points exist the rows wake up, carry a cursor, and a click or confirm buys.
void statRow(const char* name, int value, float cx, float y, bool hot, bool spendable)
{
    const float lh = screen_style::lineHeight();
    if (hot && spendable)
        screen_style::text("-", cx - lh * 7.2f, y, screen_style::kAccent);
    screen_style::text(name, cx - lh * 6.0f, y, hot ? screen_style::kTextHot : screen_style::kText);
    screen_style::text(std::to_string(value), cx + lh * 4.0f, y, screen_style::kTextHot);
    if (spendable)
        screen_style::text("+", cx + lh * 6.0f, y,
                           hot ? screen_style::kAccent : screen_style::kTextDim);
}

void renderSheet(EntityManager& em, const Mouse& mouse, float cx, float y)
{
    const float lh = screen_style::lineHeight();
    const entt::entity pl = player::entity();
    if (!em.registry().valid(pl) || !em.registry().all_of<Stats>(pl))
    {
        screen_style::textCentered("no job underway", cx, y, screen_style::kTextDim);
        return;
    }
    if (sSpendRequested)
    {
        reward::spend(em, sStatCursor);
        sSpendRequested = false;
    }
    const auto& s = em.registry().get<Stats>(pl);
    const bool spendable = reward::atRest(em) && reward::banked(em) >= reward::costOfNext(em);

    const float rowH = lh * 1.5f;
    const int values[5] = {s.chemical, s.physical, s.biological, s.endurance, s.inspection};
    const char* names[5] = {"Chemical", "Physical", "Biological", "Endurance", "Inspection"};
    for (int i = 0; i < 5; ++i)
    {
        const float rowY = y + rowH * static_cast<float>(i);
        // Mouse owns the cursor when it moves over a row; a click buys, same as confirm.
        const screen_style::Rect r{cx - lh * 7.5f, rowY - lh * 0.3f, lh * 15.0f, lh};
        if (spendable && screen_style::hit(r, mouse.x, mouse.y))
        {
            sStatCursor = i;
            if (mouse.clicked)
                reward::spend(em, i);
        }
        statRow(names[i], values[i], cx, rowY, spendable && i == sStatCursor, spendable);
    }

    // The purse and the price, always; the sales pitch only where points are actually sold.
    if (spendable)
        screen_style::textCentered("banked " + std::to_string(reward::banked(em)) +
                                       "   next point " + std::to_string(reward::costOfNext(em)) +
                                       " -- confirm or click to buy",
                                   cx, y - lh * 1.3f, screen_style::kAccent);
    else
        screen_style::textCentered("banked " + std::to_string(reward::banked(em)) +
                                       "   next point " + std::to_string(reward::costOfNext(em)) +
                                       (reward::atRest(em) ? "" : "   (sold at the rest spot)"),
                                   cx, y - lh * 1.3f, screen_style::kTextDim);

    // The derived line: what the five above actually buy. Shown so the sheet teaches its own
    // formulas -- raise Endurance and watch which numbers move.
    float dy = y + rowH * 5.6f;
    screen_style::textCentered("level " + std::to_string(stats::level(s)) + "    hp " +
                                   std::to_string(stats::maxHealth(s)) + "    stamina " +
                                   std::to_string(static_cast<int>(stats::maxStamina(s))) +
                                   "    defense " + std::to_string(stats::defense(s)),
                               cx, dy, screen_style::kTextDim);

    if (!tools::all().empty())
    {
        const auto& held = tools::all()[static_cast<size_t>(tools::selected())];
        dy += rowH;
        const int dmg = static_cast<int>(tools::damageOf(held, s) * 10.0f);
        screen_style::textCentered(held.name + "   " + std::to_string(dmg / 10) + "." +
                                       std::to_string(dmg % 10) + " per hit in these hands",
                                   cx, dy, screen_style::kTextDim);
    }
}

Action renderSystem(const Mouse& mouse, float cx, float y)
{
    const float lh = screen_style::lineHeight();
    const float rowH = lh * 1.6f;

    // Hover before the draw, so the highlight matches the mouse this frame.
    Action committed = Action::None;
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

} // namespace

void reset()
{
    sTab = 0;
    sCursor = 0;
}

Action step(bool up, bool down, bool left, bool right, bool confirm, bool back)
{
    if (back)
        return Action::Resume; // Esc always gets you out, from anywhere on the page
    if (left)
        sTab = (sTab - 1 + kTabCount) % kTabCount;
    if (right)
        sTab = (sTab + 1) % kTabCount;
    if (sTab == 0)
    {
        // The sheet only answers the keys while there is something to spend; spending itself
        // happens in render's frame, where the world is in reach.
        if (up)
            sStatCursor = (sStatCursor - 1 + 5) % 5;
        if (down)
            sStatCursor = (sStatCursor + 1) % 5;
        sSpendRequested = confirm;
        return Action::None;
    }
    if (up)
        sCursor = (sCursor - 1 + kCount) % kCount;
    if (down)
        sCursor = (sCursor + 1) % kCount;
    if (confirm)
        return kActions[static_cast<std::size_t>(sCursor)];
    return Action::None;
}

Action render(EntityManager& em, const Mouse& mouse, int windowW, int windowH)
{
    screen_style::dim(windowW, windowH);

    const float cx = static_cast<float>(windowW) * 0.5f;
    const float lh = screen_style::lineHeight();
    float y = static_cast<float>(windowH) * 0.2f;

    screen_style::headingCentered("PAUSED", cx, y, screen_style::kText);
    y += lh * 2.0f;

    // The tab strip. Clickable, and the arrows walk it; the current tab is the hot one.
    const float tabW = lh * 6.0f;
    const float stripX = cx - tabW * static_cast<float>(kTabCount) * 0.5f;
    for (int i = 0; i < kTabCount; ++i)
    {
        const float tx = stripX + tabW * (static_cast<float>(i) + 0.5f);
        const screen_style::Rect r{tx - tabW * 0.5f, y - lh * 0.3f, tabW, lh * 1.2f};
        if (screen_style::hit(r, mouse.x, mouse.y) && mouse.clicked)
            sTab = i;
        screen_style::textCentered(kTabs[static_cast<std::size_t>(i)], tx, y,
                                   i == sTab ? screen_style::kTextHot : screen_style::kTextDim);
    }
    y += lh * 2.2f;

    if (sTab == 0)
    {
        renderSheet(em, mouse, cx, y);
        return Action::None;
    }
    return renderSystem(mouse, cx, y);
}

} // namespace pause_screen
