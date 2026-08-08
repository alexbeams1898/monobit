#include "PauseScreen.h"

#include "Player.h"
#include "Stats.h"
#include "Tools.h"
#include "ecs/Components.h"
#include "ecs/EntityManager.h"

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

// One stat row: name left, value right of centre. A sheet reads as a document, not a menu --
// nothing here is selectable until there are points to spend.
void statRow(const char* name, int value, float cx, float y)
{
    const float lh = screen_style::lineHeight();
    screen_style::text(name, cx - lh * 6.0f, y, screen_style::kText);
    screen_style::text(std::to_string(value), cx + lh * 4.0f, y, screen_style::kTextHot);
}

void renderSheet(EntityManager& em, float cx, float y)
{
    const float lh = screen_style::lineHeight();
    const entt::entity pl = player::entity();
    if (!em.registry().valid(pl) || !em.registry().all_of<Stats>(pl))
    {
        screen_style::textCentered("no job underway", cx, y, screen_style::kTextDim);
        return;
    }
    const auto& s = em.registry().get<Stats>(pl);

    const float rowH = lh * 1.5f;
    statRow("Chemical", s.chemical, cx, y);
    statRow("Physical", s.physical, cx, y + rowH);
    statRow("Biological", s.biological, cx, y + rowH * 2.0f);
    statRow("Endurance", s.endurance, cx, y + rowH * 3.0f);
    statRow("Inspection", s.inspection, cx, y + rowH * 4.0f);

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
    if (sTab != 1)
        return Action::None; // only System has anything to choose
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
        renderSheet(em, cx, y);
        return Action::None;
    }
    return renderSystem(mouse, cx, y);
}

} // namespace pause_screen
