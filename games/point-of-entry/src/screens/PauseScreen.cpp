#include "screens/PauseScreen.h"

#include "formats/SpriteDefLoader.h"
#include "TextureManager.h"
#include "ecs/BalanceConfig.h"
#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "ops/GuideOps.h"
#include "ops/RecordOps.h"
#include "systems/CombatSystem.h"
#include "systems/PlayerSystem.h"
#include "systems/RewardSystem.h"
#include "systems/ThermosSystem.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

#include <entt/entt.hpp>

namespace pause_screen
{
namespace
{
// The tabs. Sheet first: pausing to look at yourself is the common case; the machine's
// business is one keypress right.
constexpr std::array<const char*, 4> kTabs{"Sheet", "Satchel", "Field Guide", "System"};
constexpr int kTabCount = 4;

// No Resume row: Esc IS resume, from anywhere on the screen, and a menu entry duplicating the
// key that opened the menu is noise. Leaving puts you back at the title; quitting closes the
// game -- both here because making a player who is done pass through the title to close a
// window is a small rudeness that costs nothing to avoid.
constexpr std::array<const char*, 3> kLabels{"Settings", "Leave the job", "Quit"};
constexpr std::array<Action, 3> kActions{Action::Settings, Action::Leave, Action::Quit};
constexpr int kCount = 3;

int sTab = 0;
int sCursor = shell_input::kNoChoice;
int sStatCursor = shell_input::kNoChoice; // which sheet row, while there are points to spend
bool sSpendRequested = false;             // confirm pressed on the sheet this frame

// One stat row: name at the column's left edge, value at its right -- the shape of a form,
// which is what a sheet is. A document while there is nothing to spend; the moment points
// exist the rows wake up, carry a cursor, and a click or confirm buys.
void statRow(const char* name, int value, const screen_style::Rect& row, float y, bool hot,
             bool spendable)
{
    screen_style::link(name, row.x + screen_style::pad(3), y,
                       hot ? screen_style::LinkState::Hot : screen_style::LinkState::Idle);
    // A form's value column: the numbers line up with each other instead of each sitting
    // wherever its own name happened to end.
    screen_style::textRight(std::to_string(value), screen_style::pageStop(row, 0.62f), y,
                            screen_style::kTextHot);
    if (spendable)
        screen_style::text("+", screen_style::pageStop(row, 0.70f), y,
                           hot ? screen_style::kAccent : screen_style::kTextDim);
}

void renderSheet(EntityManager& em, const shell_input::Mouse& mouse, float cx, float y)
{
    const entt::entity pl = player::entity();
    if (!em.registry().valid(pl) || !em.registry().all_of<Stats>(pl))
    {
        screen_style::textCentered("no job underway", cx, y, screen_style::kTextDim);
        return;
    }
    const auto& s = em.registry().get<Stats>(pl);
    const bool spendable = false; // the sheet is a READOUT; points change hands at the staging

    const float rowH = screen_style::pageRowH();
    // A tab is handed where its content begins and lays out DOWNWARD from it. Reaching back
    // above that line is reaching into the page's head, which is where the top margin went.
    screen_style::textCentered("banked " + std::to_string(reward::banked(em)) + "   next point " +
                                   std::to_string(reward::costOfNext(em)) +
                                   "   (sold at the staging area)",
                               cx, y, screen_style::kTextDim);
    const float rowsY = y + rowH;

    const int values[5] = {s.chemical, s.physical, s.biological, s.endurance, s.inspection};
    const char* names[5] = {"Chemical", "Physical", "Biological", "Endurance", "Inspection"};
    for (int i = 0; i < 5; ++i)
    {
        const float rowY = rowsY + rowH * static_cast<float>(i);
        const screen_style::Rect row = screen_style::pageRow(cx, rowY);
        // Mouse owns the cursor when it moves over a row; a click buys, same as confirm.
        if (spendable && screen_style::hit(row, mouse.x, mouse.y))
        {
            sStatCursor = i;
            if (mouse.clicked)
                reward::spend(em, i);
        }
        statRow(names[i], values[i], row, rowY, spendable && i == sStatCursor, spendable);
    }

    // The derived line: what the five above actually buy. Shown so the sheet teaches its own
    // formulas -- raise Endurance and watch which numbers move.
    float dy = rowsY + rowH * 5.4f;
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

// What he is carrying: a receipt, not a menu -- name, quality, count. Nothing here is usable
// yet; using things arrives with crafting.
void renderSatchel(EntityManager& em, float cx, float y)
{
    const entt::entity pl = player::entity();
    if (!em.registry().valid(pl) || !em.registry().all_of<Satchel>(pl) ||
        em.registry().get<Satchel>(pl).items.empty())
    {
        screen_style::textCentered("nothing collected", cx, y, screen_style::kTextDim);
        return;
    }
    static constexpr const char* kQuality[4] = {"crude", "standard", "fine", "superior"};
    float rowY = y;
    for (const auto& held : em.registry().get<Satchel>(pl).items)
    {
        const ItemDef& def = items::get(held.item);
        const screen_style::Rect row = screen_style::pageRow(cx, rowY);
        screen_style::text(def.name, row.x + screen_style::pad(3), rowY, screen_style::kText);
        screen_style::textRight(kQuality[static_cast<int>(held.quality)],
                                screen_style::pageStop(row, 0.8f), rowY, screen_style::kTextDim);
        screen_style::textRight("x" + std::to_string(held.count),
                                row.x + row.w - screen_style::pad(2), rowY, screen_style::kTextHot);
        rowY += screen_style::pageRowH();
    }
}

// THE FIELD GUIDE. Re-read from disk each opening of the screen, so an edited file is live
// on the next pause. The pane takes this share of the column; the register takes the rest.
constexpr float kPaneShare = 0.62f;

std::vector<guide::Page> sGuidePages;
std::vector<sprite_def::Def> sGuideArt; // parallel to sGuidePages: each page's plate
guide::Gates sGuideGates;
bool sGuideLoaded = false;
int sGuideCursor = shell_input::kNoChoice;
int sGuideRows = 0;          // species listed as last drawn, for the keyboard to walk
int sGuideTop = 0;           // first species the register's window shows
bool sGuideKeyMoved = false; // the cursor walked by key, so the window must follow it

void ensureGuide()
{
    if (sGuideLoaded)
        return;
    sGuidePages = guide::scan("config/pests");
    sGuideArt.clear();
    sGuideArt.reserve(sGuidePages.size());
    for (const auto& page : sGuidePages)
        sGuideArt.push_back(page.sprite.empty() ? sprite_def::Def{}
                                                : sprite_def::load(page.sprite));
    sGuideGates = guide::gates("config/stats.json");
    sGuideLoaded = true;
}

// A page's plate: the species' own art, first frame, top-centred at (cx, top) on a whole-
// number scale -- the page obeys the same integer law as the world. Returns the drawn height;
// 0 when there is no art or no whole scale fits the budget.
float plate(TextureManager& tm, const sprite_def::Def& def, float cx, float top, float maxH)
{
    if (!def.ok || def.frame_h <= 0)
        return 0.0f;
    const int s = static_cast<int>(maxH) / def.frame_h;
    if (s < 1)
        return 0.0f;
    const uint32_t tex = tm.load(def.sheet);
    int sheetW = 0;
    int sheetH = 0;
    tm.getDimensions(def.sheet, sheetW, sheetH);
    if (sheetW <= 0 || sheetH <= 0)
        return 0.0f;
    const float w = static_cast<float>(def.frame_w * s);
    const float h = static_cast<float>(def.frame_h * s);
    // The IDLE pose is the reference drawing -- what the species looks like standing still, the
    // way a printed guide shows it. Falling back to the first frame keeps a species with no idle
    // on its own art readable.
    const int pose = std::max(0, sprite_def::frameOf(def, "idle"));
    const float u0 = static_cast<float>(def.frame_w * pose) / static_cast<float>(sheetW);
    UIRenderer::drawTexturedRect({std::floor(cx - w * 0.5f), std::floor(top), w, h}, tex,
                                 {u0, 0.0f,
                                  static_cast<float>(def.frame_w) / static_cast<float>(sheetW),
                                  static_cast<float>(def.frame_h) / static_cast<float>(sheetH)});
    return h;
}

// A species the record has never turned up is a place in the book, not a page: the cursor
// steps over it and the pane it would fill stays as it was.
bool documented(int i)
{
    if (i < 0 || i >= static_cast<int>(sGuidePages.size()))
        return false;
    const auto& page = sGuidePages[static_cast<std::size_t>(i)];
    return guide::tier(record::kills(page.species), sGuideGates) != guide::Tier::Undocumented;
}

// THE REGISTER: a bare list of what the trade knows about, id and name, nothing else. The
// numbers are the guide's own -- a species' place in the book, which never moves.
void renderGuideList(const shell_input::Mouse& mouse, const screen_style::Rect& band, bool keyMoved)
{
    const float rowH = screen_style::pageRowH();
    const int rows = static_cast<int>(sGuidePages.size());
    sGuideRows = rows;
    if (sGuideCursor >= rows)
        sGuideCursor = shell_input::kNoChoice;
    if (rows == 0)
    {
        screen_style::text("nothing on file", band.x + screen_style::pad(3), band.y,
                           screen_style::kTextDim);
        return;
    }

    const int visible = std::max(1, static_cast<int>(band.h / rowH));
    sGuideTop = shell_input::scrollTop(sGuideTop, rows, visible, mouse.wheel,
                                       keyMoved ? sGuideCursor : shell_input::kNoChoice);

    // The pointer decides before anything is drawn, so what lights up is what the mouse is on
    // this frame -- but only over a species the book actually has a page for.
    const auto rowY = [&](int slot) { return band.y + rowH * static_cast<float>(slot); };
    int over = shell_input::kNoChoice;
    for (int slot = 0; slot < visible && sGuideTop + slot < rows; ++slot)
        if (documented(sGuideTop + slot) &&
            screen_style::hit(screen_style::bandRow(band, rowY(slot)), mouse.x, mouse.y))
            over = sGuideTop + slot;
    sGuideCursor = shell_input::hover(sGuideCursor, over, mouse.moved);

    // The name column begins where the number column really ends, measured off the widest number
    // the register holds: a guessed gap collides the day the book reaches a hundred species.
    const float numberX = band.x + screen_style::pad(2);
    const float nameX =
        numberX + screen_style::widthOf(guide::number(rows - 1)) + screen_style::pad(2);
    for (int slot = 0; slot < visible && sGuideTop + slot < rows; ++slot)
    {
        const int i = sGuideTop + slot;
        const auto& page = sGuidePages[static_cast<std::size_t>(i)];
        const bool known = documented(i);
        screen_style::text(guide::number(i), numberX, rowY(slot), screen_style::kTextDim);
        // Not yet earned: the register keeps the place, not the name.
        screen_style::link(known ? page.name : "---------", nameX, rowY(slot),
                           i == sGuideCursor ? screen_style::LinkState::Hot
                           : known           ? screen_style::LinkState::Idle
                                             : screen_style::LinkState::Faint);
    }

    // Where the window sits, drawn only once the register runs past it.
    if (rows > visible)
        screen_style::textRight(
            std::to_string(sGuideTop + 1) + "-" +
                std::to_string(std::min(rows, sGuideTop + visible)) + " of " + std::to_string(rows),
            band.x + band.w - screen_style::pad(2), band.y + band.h - rowH, screen_style::kTextDim);
}

// THE PANE: the selected species' page, on paper. An empty selection leaves an empty sheet --
// there is no page for a thing nobody has met.
void renderGuidePane(TextureManager& tm, const screen_style::Rect& card)
{
    screen_style::paperPanel(card);
    if (!documented(sGuideCursor))
        return;

    const auto& page = sGuidePages[static_cast<std::size_t>(sGuideCursor)];
    const float lh = screen_style::lineHeight();
    const float cx = card.x + card.w * 0.5f;
    // Clear of the sheet's margin rule, the way a hand writing on ruled paper does.
    const float textX = card.x + screen_style::pad(5);
    const int killed = record::kills(page.species);

    float rowY = card.y + screen_style::pad(3);
    screen_style::inkTextCentered(page.name, cx, rowY, screen_style::kInk);
    rowY += lh * 1.3f;
    screen_style::inkTextCentered("killed " + std::to_string(killed), cx, rowY,
                                  screen_style::kInkDim);
    rowY += lh * 1.3f;

    const float plateH =
        plate(tm, sGuideArt[static_cast<std::size_t>(sGuideCursor)], cx, rowY, lh * 2.5f);
    if (plateH > 0.0f)
        rowY += plateH + lh * 0.6f;

    static constexpr const char* kSections[6] = {"Description",        "Similar Groups", "Biology",
                                                 "Habits & Harborage", "Signs",          "Control"};
    const std::string* bodies[6] = {&page.entry.description, &page.entry.similar,
                                    &page.entry.biology,     &page.entry.habits,
                                    &page.entry.signs,       &page.entry.control};
    for (int i = 0; i < 6; ++i)
    {
        // The sheet is a fixed size; what does not fit on it is not printed over its edge.
        if (rowY > card.y + card.h - lh * 2.0f)
            return;
        screen_style::inkText(kSections[i], textX, rowY, screen_style::kInk);
        rowY += lh * 1.2f;
        if (!bodies[i]->empty())
        {
            screen_style::inkText(*bodies[i], textX + screen_style::pad(2), rowY,
                                  screen_style::kInkDim);
            rowY += lh * 1.2f;
        }
    }
    if (guide::tier(killed, sGuideGates) == guide::Tier::Stats)
        screen_style::inkTextCentered("resistance " + std::to_string(page.resistance) +
                                          "   defensiveness " + std::to_string(page.defensiveness) +
                                          "   dispersal " + std::to_string(page.dispersal),
                                      cx, rowY + lh * 0.4f, screen_style::kInkDim);
}

// The tab is one page divided: the selection's own page on the left, the register of every
// species on the right. Reading IS moving the cursor -- there is nothing here to open.
void renderGuide(TextureManager& tm, const shell_input::Mouse& mouse, float y, int windowW,
                 int windowH)
{
    ensureGuide();
    const screen_style::Split split = screen_style::pageSplit(
        windowW, y - screen_style::pad(2), screen_style::pageBottom(windowH), kPaneShare);
    renderGuidePane(tm, split.left);
    renderGuideList(mouse, split.right, sGuideKeyMoved);
    sGuideKeyMoved = false;
}

Action renderSystem(const shell_input::Mouse& mouse, float cx, float y)
{
    const float rowH = screen_style::pageRowH();

    // Hover before the draw, so what is lit matches the mouse this frame.
    Action committed = Action::None;
    int over = shell_input::kNoChoice;
    for (int i = 0; i < kCount; ++i)
        if (screen_style::hit(screen_style::pageRow(cx, y + rowH * static_cast<float>(i)), mouse.x,
                              mouse.y))
        {
            over = i;
            if (mouse.clicked)
                committed = kActions[static_cast<std::size_t>(i)];
        }
    sCursor = shell_input::hover(sCursor, over, mouse.moved);
    for (int i = 0; i < kCount; ++i)
        screen_style::entry(kLabels[static_cast<std::size_t>(i)], cx,
                            y + rowH * static_cast<float>(i), i == sCursor, /*enabled=*/true);
    return committed;
}

} // namespace

void reset()
{
    sTab = 0;
    sCursor = shell_input::kNoChoice;
    sGuideLoaded = false; // the book re-reads its files each opening
    sGuideCursor = shell_input::kNoChoice;
    sGuideTop = 0;
}

Action step(bool up, bool down, bool left, bool right, bool confirm, bool back)
{
    if (back)
        return Action::Resume;
    if (left)
        sTab = (sTab - 1 + kTabCount) % kTabCount;
    if (right)
        sTab = (sTab + 1) % kTabCount;
    if (sTab == 0)
        return Action::None; // the sheet is a readout
    if (sTab == 1)
        return Action::None; // the satchel is a receipt; nothing to choose yet
    if (sTab == 2)
    {
        // The cursor wraps in render, against however many rows were actually drawn.
        // The count comes from the register as last drawn; render clears a cursor past
        // its end.
        sGuideCursor = shell_input::stepOver(sGuideCursor, sGuideRows, up, down, documented);
        sGuideKeyMoved = up || down;
        return Action::None;
    }
    sCursor = shell_input::step(sCursor, kCount, up, down);
    if (confirm && sCursor != shell_input::kNoChoice)
        return kActions[static_cast<std::size_t>(sCursor)];
    return Action::None;
}

Action render(EntityManager& em, TextureManager& tm, const shell_input::Mouse& mouse, int windowW,
              int windowH)
{
    screen_style::dim(windowW, windowH);
    // The page's backing panel, so the content reads as one object over the frozen world.
    screen_style::panel(screen_style::pagePanelRect(windowW, windowH));

    const float cx = static_cast<float>(windowW) * 0.5f;
    const float lh = screen_style::lineHeight();
    float y = screen_style::pageHeadingY(windowH);

    // The strip IS this page's head -- the lit tab already says where you are, so there is no
    // title above it. The column divided, so the tabs can never reach past the page they sit
    // on; clickable, and the arrows walk it.
    for (int i = 0; i < kTabCount; ++i)
    {
        const screen_style::Rect tab = screen_style::pageTab(cx, y, i, kTabCount);
        const bool over = screen_style::hit(tab, mouse.x, mouse.y);
        if (over && mouse.clicked)
            sTab = i;
        // The tab you are ON takes the marks; one you are merely pointing at only brightens.
        screen_style::linkCentered(kTabs[static_cast<std::size_t>(i)], tab.x + tab.w * 0.5f, y,
                                   i == sTab ? screen_style::LinkState::Hot
                                   : over    ? screen_style::LinkState::Idle
                                             : screen_style::LinkState::Faint);
    }
    y += lh * 2.2f;

    if (sTab == 0)
    {
        renderSheet(em, mouse, cx, y);
        return Action::None;
    }
    if (sTab == 1)
    {
        renderSatchel(em, cx, y);
        return Action::None;
    }
    if (sTab == 2)
    {
        renderGuide(tm, mouse, y, windowW, windowH);
        return Action::None;
    }
    return renderSystem(mouse, cx, y);
}

} // namespace pause_screen
