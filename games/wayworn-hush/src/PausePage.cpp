#include "PausePage.h"

#include "FontManager.h"
#include "Inventory.h"
#include "Notebook.h"
#include "ReadingColor.h"
#include "ScreenInput.h"
#include "ScreenStyle.h"
#include "UIRenderer.h"

#include <algorithm>
#include <string>

namespace pause_page
{
namespace
{
FontHandle sFont = -1;

constexpr int kTabCount = 5; // Self, Satchel, Craft, Notebook, System

// System-tab menu items.
constexpr int kSysControls = 0;
constexpr int kSysSettings = 1; // how you like the game -- the same screen the title opens
constexpr int kSysLeave = 2;    // back to the title -- the walk is kept
constexpr int kSysQuit = 3;     // out of the game entirely -- also kept
constexpr int kSysItemCount = 4;

// Wayworn's minimal register: muted shadow-text over a soft darkening of the
// frozen world. Colors mirror the thought box (AESTHETIC.md).
constexpr Color kOverlay{0.04f, 0.05f, 0.06f, 0.92f};
constexpr Color kText{0.95f, 0.95f, 0.92f, 1.0f};
constexpr Color kTextDim{0.95f, 0.95f, 0.92f, 0.45f};
constexpr Color kShadow{0.0f, 0.0f, 0.0f, 0.55f};

// Tab chrome -- soft, low-contrast fills (not the roguelike's saturated panels).
// The active tab reads brighter and carries an underline accent.
constexpr Color kTabActive{0.16f, 0.17f, 0.19f, 0.85f};
constexpr Color kTabInactive{0.09f, 0.10f, 0.11f, 0.6f};
constexpr Color kTabHover{0.13f, 0.14f, 0.16f, 0.8f};
constexpr Color kTabBaseline{0.95f, 0.95f, 0.92f, 0.18f};
constexpr Color kTabAccent{0.95f, 0.95f, 0.92f, 0.85f};

// Draw shadow-then-text, the whole game's legible-over-world idiom. `alpha`
// scales both so callers can dim inactive items.
void softText(const std::string& s, float x, float y, const Color& c, float alpha = 1.0f)
{
    if (sFont < 0)
        return;
    UIRenderer::drawText(sFont, s, x + 2.0f, y + 2.0f,
                         {kShadow.r, kShadow.g, kShadow.b, kShadow.a * alpha});
    UIRenderer::drawText(sFont, s, x, y, {c.r, c.g, c.b, c.a * alpha});
}

// Centered variant.
void softTextCentered(const std::string& s, float cx, float y, const Color& c, float alpha = 1.0f)
{
    const auto ts = UIRenderer::measureText(sFont, s);
    softText(s, cx - ts.width * 0.5f, y, c, alpha);
}

float lineH()
{
    return static_cast<float>(FontManager::lineHeight(sFont)) + 8.0f;
}

// The tab label pad (each side). Tab width = text width + 2*pad. Shared by the
// layout pre-pass (hit-testing) and the draw pass so they never disagree.
constexpr float kTabPadX = 22.0f;

float tabWidth(const std::string& label)
{
    return UIRenderer::measureText(sFont, label).width + kTabPadX * 2.0f;
}

// A single tab: soft fill, centered label, underline accent when active. The
// hovered (but inactive) tab lifts slightly so mouse feedback reads.
void drawTab(const std::string& label, float x, float y, float w, float h, bool active,
             bool hovered)
{
    const auto ts = UIRenderer::measureText(sFont, label);
    Color fill = active ? kTabActive : kTabInactive;
    if (!active && hovered)
        fill = kTabHover;
    UIRenderer::drawRect(x, y, w, h, fill);
    softText(label, x + kTabPadX, y + (h - ts.height) * 0.5f, kText,
             active ? 1.0f : (hovered ? 0.75f : 0.45f));
    if (active)
        UIRenderer::drawRect(x, y + h - 3.0f, w, 3.0f, kTabAccent);
}

// --- Tab content ---------------------------------------------------------

void renderSelf(const growth::GrowthState& g, float cx, float y)
{
    softTextCentered("Spirit  " + std::to_string(growth::spirit(g)), cx, y, kText);
    y += lineH() * 1.6f;
    // The reading-self (tier 1): faculty level = base + buffs.
    for (const auto& faculty : g.faculties)
    {
        softTextCentered(faculty + "   " + std::to_string(growth::facultyLevel(g, faculty)), cx, y,
                         kText);
        y += lineH();
    }
    // The doing layer (tier 2): survival / craftsmanship base values, dimmer to
    // read as secondary.
    if (!g.secondary.empty())
    {
        y += lineH() * 0.6f;
        for (const auto& stat : g.secondary)
        {
            softTextCentered(stat + "   " + std::to_string(growth::statLevel(g, stat)), cx, y,
                             kTextDim);
            y += lineH();
        }
    }
}

// --- the shared item grid (Souls STRUCTURE -- icons in a grid + a detail panel -- in
// wayworn's MINIMAL/CUTE register: soft cells, a gentle cursor, no ornate chrome) ----------

// One list entry: an item id + a quantity + whether it's marked (in the pot -> green) or new.
// `show_count` forces the "x N" readout even for N<=1 (the Craft columns always show counts so
// the pilgrim can read "thyme x3"; the plain Satchel leaves it off so lone items read quiet).
struct GridItem
{
    std::string id;
    int qty = 0;
    bool marked = false;
    bool is_new = false;
    bool show_count = false;
};

// Soft row highlights (cute/minimal -- low contrast, warm, no hard borders). Only the cursored
// / marked rows get a fill; resting rows are bare text so the list reads quiet.
constexpr Color kCellCursor{0.30f, 0.29f, 0.24f, 0.85f}; // the cursored row (warm lift)
constexpr Color kCellMarked{0.22f, 0.28f, 0.22f, 0.8f};  // a toggle-selected row (soft green)

// Word-wrap `text` to `maxW` px, handing each line to `draw` at its left-aligned (x,y) and
// returning the y past the last one. Long words that don't fit alone are left overflowing
// (rare for this copy) rather than hard-split mid-word. Taking the draw as a parameter is
// what lets shadow-text over the world and plain ink on paper share ONE line-breaking rule
// -- two copies of this would eventually wrap the same sentence differently.
template <typename DrawLine>
float wrapLines(const std::string& text, float x, float y, float maxW, const DrawLine& draw)
{
    std::string line;
    std::size_t i = 0;
    while (i < text.size())
    {
        // Pull the next word (up to the following space).
        const std::size_t sp = text.find(' ', i);
        const std::string word = text.substr(i, sp == std::string::npos ? sp : sp - i);
        std::string trial = line;
        if (!trial.empty())
            trial += ' ';
        trial += word;
        if (!line.empty() && UIRenderer::measureText(sFont, trial).width > maxW)
        {
            draw(line, x, y);
            y += lineH();
            line = word;
        }
        else
            line = trial;
        i = (sp == std::string::npos) ? text.size() : sp + 1;
    }
    if (!line.empty())
    {
        draw(line, x, y);
        y += lineH();
    }
    return y;
}

// Wrapped shadow-text -- the register for anything drawn over the world.
float softTextWrapped(const std::string& text, float x, float y, float maxW, const Color& c,
                      float alpha = 1.0f)
{
    return wrapLines(text, x, y, maxW, [&](const std::string& s, float lx, float ly)
                     { softText(s, lx, ly, c, alpha); });
}

// Wrapped plain text -- the register for ink on a paper panel.
float inkTextWrapped(const std::string& text, float x, float y, float maxW, const Color& c)
{
    return wrapLines(text, x, y, maxW, [&](const std::string& s, float lx, float ly)
                     { UIRenderer::drawText(sFont, s, lx, ly, c); });
}

// A list row's full height (icon chip + name). Shared by every list so hit-testing and drawing
// agree on where each row sits.
float listRowH()
{
    return lineH() * 1.35f;
}

// The list region reserves a FIXED number of rows of vertical space, so controls anchored below
// it (Combine, the detail panel) stay put regardless of how many items the list holds. Lists
// longer than this scroll within the region (a later refinement); shorter lists leave the rest
// of the region empty rather than pulling the controls up.
constexpr int kListRows = 6;

// The pixel height the list region always occupies (kListRows rows).
float listRegionH()
{
    return static_cast<float>(kListRows) * listRowH();
}

// The window size, bundled so layout code passes one thing (both dims are always needed together
// to derive the 16:9 safe area).
struct Canvas
{
    int w = 0;
    int h = 0;
};

// The page's content band width: a fraction of the 16:9 SAFE AREA (not the raw window), so it
// scales with resolution like the rest of the HUD and stays a cohesive centered panel on a wide
// window instead of sprawling to the edges. Shared by every tab's layout.
float contentBandW(Canvas c)
{
    return hud::scale(c.w, c.h) * 0.72f;
}

// The row index the mouse is over within a vertical list of `count` rows starting at (x,y),
// or -1 if none. The single hit-test all lists share, mirroring the tab strip / System menu
// mouse standard (pointInRect per element).
int listRowAt(const Mouse& mouse, float x, float y, float w, int count)
{
    const float rowH = listRowH();
    for (int i = 0; i < count; ++i)
        if (engine::ui::pointInRect(mouse.x, mouse.y, x, y + static_cast<float>(i) * rowH, w, rowH))
            return i;
    return -1;
}

// One list row: a small icon chip + the item name, left-aligned at (x,y). The active row
// (keyboard cursor OR mouse hover) gets a soft warm fill; marked (in-the-pot) rows read green.
// `rowH` is the row's full height; the chip is inset within it.
void drawListRow(const inventory::ItemDef* def, const GridItem& gi, const IconResolver& icon,
                 float x, float y, float rowW, float rowH, bool active)
{
    if (active || gi.marked)
        UIRenderer::drawRect(x, y, rowW, rowH, gi.marked ? kCellMarked : kCellCursor);

    const float chip = rowH * 0.78f;
    const float chipY = y + (rowH - chip) * 0.5f;
    const float chipX = x + rowH * 0.14f;
    const std::uint32_t tex = def ? icon(def->icon) : 0u;
    if (tex != 0u)
        UIRenderer::drawTexturedRect({chipX, chipY, chip, chip}, tex);
    else if (def) // no texture -> a soft rarity swatch stands in
        UIRenderer::drawRect(chipX, chipY, chip, chip,
                             reading_color::rarityColor(def->rarity, 0.7f));

    const std::string name = def ? def->name : gi.id;
    const auto ts = UIRenderer::measureText(sFont, name);
    const float textX = chipX + chip + rowH * 0.22f;
    softText(name, textX, y + (rowH - ts.height) * 0.5f, kText, active ? 1.0f : 0.7f);

    if (gi.qty > 1 || (gi.show_count && gi.qty >= 0))
    {
        const std::string q = "x" + std::to_string(gi.qty);
        softText(q, x + rowW - UIRenderer::measureText(sFont, q).width - rowH * 0.2f,
                 y + (rowH - ts.height) * 0.5f, kTextDim);
    }
    if (gi.is_new) // a small "New" pip at the row's leading edge
        UIRenderer::drawRect(x + 2.0f, y + rowH * 0.5f - 3.0f, 5.0f, 5.0f,
                             {0.98f, 0.82f, 0.38f, 0.95f});
}

// Draw a vertical item list at (x,y): one drawListRow per item, the `active` row highlighted.
// Pure draw -- the caller owns hit-testing (listRowAt) so a list can be read-only or interactive.
void drawList(const std::vector<GridItem>& items, const inventory::Registry& reg,
              const IconResolver& icon, int active, float x, float y, float w,
              const std::string& emptyMsg)
{
    if (items.empty())
    {
        softText(emptyMsg, x, y, kTextDim);
        return;
    }
    const float rowH = listRowH();
    for (int i = 0; i < static_cast<int>(items.size()); ++i)
        drawListRow(reg.find(items[static_cast<std::size_t>(i)].id),
                    items[static_cast<std::size_t>(i)], icon, x, y + static_cast<float>(i) * rowH,
                    w, rowH, i == active);
}

// The right-hand detail panel for the item at `sel` in `items`: name (rarity-colored) + wrapped
// description, drawn from (x,y) within width `w`. No-op if sel is out of range.
void drawItemDetail(const std::vector<GridItem>& items, const inventory::Registry& reg, int sel,
                    float x, float y, float w)
{
    if (sel < 0 || sel >= static_cast<int>(items.size()))
        return;
    const inventory::ItemDef* def = reg.find(items[static_cast<std::size_t>(sel)].id);
    const std::string name = def ? def->name : items[static_cast<std::size_t>(sel)].id;
    softText(name, x, y, def ? reading_color::rarityColor(def->rarity) : kText);
    y += lineH() * 1.6f;
    if (def && !def->description.empty())
        softTextWrapped(def->description, x, y, w, kTextDim);
}

// The Satchel view: a single item list on the LEFT + a detail panel on the RIGHT for the row
// under the keyboard cursor or the mouse. Hovering a row moves the cursor (so the detail follows
// the mouse), matching the page's other mouse-driven surfaces. Read-only -- no click action.
// What the Satchel view needs to draw itself, bundled like CraftView so the signature
// stays readable as the tab grows.
struct SatchelView
{
    const std::vector<GridItem>& items;
    const inventory::Registry& reg;
    const IconResolver& icon;
    const Mouse& mouse;
};

void renderSatchel(PauseState& pause, const SatchelView& v, float cx, float y, Canvas canvas)
{
    const std::vector<GridItem>& items = v.items;
    const Mouse& mouse = v.mouse;
    const float contentW = contentBandW(canvas);
    const float leftX = cx - contentW * 0.5f;
    const float colGap = contentW * 0.08f;
    const float listW = (contentW - colGap) * 0.5f;
    const float rightX = leftX + listW + colGap;
    const float rightW = contentW - listW - colGap;

    if (!items.empty())
        pause.satchel_sel = std::clamp(pause.satchel_sel, 0, static_cast<int>(items.size()) - 1);
    const int hover = listRowAt(mouse, leftX, y, listW, static_cast<int>(items.size()));
    if (hover >= 0)
        pause.satchel_sel = hover; // hovering a row selects it (detail follows the mouse)

    const int sel = items.empty() ? -1 : pause.satchel_sel;
    drawList(items, v.reg, v.icon, sel, leftX, y, listW, "Nothing yet");
    drawItemDetail(items, v.reg, sel, rightX, y, rightW);
}

// The Satchel grid items: everything carried, key items first (they're the meaningful ones),
// then keepsakes, then practical -- one grid, in that order, so the cursor walks it naturally.
std::vector<GridItem> satchelGrid(const inventory::Satchel& sat, const inventory::Registry& reg)
{
    const inventory::Category order[] = {inventory::Category::KeyItem,
                                         inventory::Category::Keepsake,
                                         inventory::Category::Practical};
    std::vector<GridItem> out;
    for (const auto cat : order)
        for (const auto& e : sat.items)
        {
            const inventory::ItemDef* def = reg.find(e.id);
            const inventory::Category c = def ? def->category : inventory::Category::Keepsake;
            if (c == cat)
                out.push_back(GridItem{e.id, e.quantity, false, e.is_new});
        }
    return out;
}

} // namespace

// The Craft-tab material rows: the distinct item ids the pilgrim can throw in the pot, in a
// stable order. Row order is shared by render + step so the selection cursor lines up. The pot
// takes ANYTHING you carry (Little-Alchemy: experiment freely) EXCEPT key items -- those are
// progression-bound (the notebook, the watch) and must not be consumed on a failed attempt.
std::vector<CraftMaterial> craftMaterials(const inventory::Satchel& sat,
                                          const inventory::Registry& reg)
{
    std::vector<CraftMaterial> out;
    std::unordered_set<std::string> seen;
    for (const auto& e : sat.items)
    {
        const inventory::ItemDef* def = reg.find(e.id);
        if (def && def->category == inventory::Category::KeyItem)
            continue; // key items are protected from the pot
        if (seen.insert(e.id).second)
            out.push_back(CraftMaterial{e.id, inventory::count(sat, e.id)});
    }
    return out;
}

namespace
{
// Throw one more of `id` into the pot, capped at how many the pilgrim carries.
void addToPot(PauseState& pause, const inventory::Satchel& sat, const std::string& id)
{
    const auto it = pause.craft_selected.find(id);
    const int inPot = (it == pause.craft_selected.end()) ? 0 : it->second;
    if (inPot < inventory::count(sat, id))
        pause.craft_selected[id] = inPot + 1;
}

// Take one of `id` back out of the pot (removing the key when it hits zero).
void removeFromPot(PauseState& pause, const std::string& id)
{
    const auto it = pause.craft_selected.find(id);
    if (it != pause.craft_selected.end() && --it->second <= 0)
        pause.craft_selected.erase(it);
}

// The Craft tab's read-only inputs, bundled so the render + its helpers pass one thing instead of
// five positional args (keeps every sub-function under the parameter budget).
struct CraftView
{
    const inventory::Satchel& sat;
    const inventory::Registry& reg;
    const IconResolver& icon;
    const std::vector<CraftMaterial>& materials;
    const Mouse& mouse;
};

// Two-column layout for the Craft tab, derived once from the content origin + window width.
struct CraftLayout
{
    float left_x, list_w, pot_x, pot_w, content_w, list_y;
};

CraftLayout craftLayout(float cx, float y, Canvas canvas)
{
    const float contentW = contentBandW(canvas);
    const float leftX = cx - contentW * 0.5f;
    const float colGap = contentW * 0.08f;
    const float listW = (contentW - colGap) * 0.5f;
    return {leftX,
            listW,
            leftX + listW + colGap,
            contentW - listW - colGap,
            contentW,
            y + lineH() * 1.2f};
}

// The Craft tab: throw materials in the pot, then Combine to attempt it (Little-Alchemy --
// realizing a recipe is by trying). Left = everything you can throw in (click / Space toggles it
// into the pot); right = the POT, what's going in (click a pot row to pull it back out); below
// the pot = a Combine control; the bottom spans a detail panel for the focused material + the
// last attempt's result. Keyboard focus (craft_sel) walks materials then Combine (stepCraft); the
// mouse can also drive every control. Returns Craft if Combine was clicked, else None.
Action renderCraft(PauseState& pause, const CraftView& v, float cx, float y, Canvas canvas)
{
    const CraftLayout lo = craftLayout(cx, y, canvas);
    const int nMat = static_cast<int>(v.materials.size());

    // Satchel rows (how many carried, minus what's already staged) + pot rows (how many staged).
    // Both lists follow materials order so they stay stable. A material with its whole stack in
    // the pot still shows in the Satchel list (greyed to x0) so its row keeps its place.
    std::vector<GridItem> matRows;
    std::vector<GridItem> potRows;
    std::vector<std::string> potIds; // parallel to potRows, for click-to-remove
    matRows.reserve(v.materials.size());
    for (const auto& m : v.materials)
    {
        const auto it = pause.craft_selected.find(m.id);
        const int inPot = (it == pause.craft_selected.end()) ? 0 : it->second;
        matRows.push_back(
            GridItem{m.id, m.carried - inPot, inPot > 0, false, true}); // still addable
        if (inPot > 0)
        {
            potRows.push_back(GridItem{m.id, inPot, false, false, true});
            potIds.push_back(m.id);
        }
    }

    softText("Satchel", lo.left_x, y, kTextDim);
    softText("To craft", lo.pot_x, y, kTextDim);

    // Hovering a material row focuses it (detail follows the mouse + a click has a target).
    const int matHover = listRowAt(v.mouse, lo.left_x, lo.list_y, lo.list_w, nMat);
    if (matHover >= 0)
        pause.craft_sel = matHover;
    const int focus = (pause.craft_sel < nMat) ? pause.craft_sel : -1;
    drawList(matRows, v.reg, v.icon, focus, lo.left_x, lo.list_y, lo.list_w,
             "Nothing to work with yet.");
    drawList(potRows, v.reg, v.icon, -1, lo.pot_x, lo.list_y, lo.pot_w, "(empty)");

    // A Satchel-row click throws one more of that item in the pot; a pot-row click takes one back.
    const int potHover =
        listRowAt(v.mouse, lo.pot_x, lo.list_y, lo.pot_w, static_cast<int>(potRows.size()));
    if (v.mouse.clicked && matHover >= 0)
        addToPot(pause, v.sat, v.materials[static_cast<std::size_t>(matHover)].id);
    else if (v.mouse.clicked && potHover >= 0)
        removeFromPot(pause, potIds[static_cast<std::size_t>(potHover)]);

    // Combine + the detail panel anchor BELOW the fixed-height list region, so they hold their
    // place no matter how many items the list carries (the region is roomy; long lists scroll
    // within it later). Combine sits under the pot column; the detail spans the band below both.
    const float combineY = lo.list_y + listRegionH() + lineH() * 0.5f;
    const bool combineHover =
        engine::ui::pointInRect(v.mouse.x, v.mouse.y, lo.pot_x, combineY - 4.0f, lo.pot_w, lineH());
    softText("\xE2\x96\xB8 Combine", lo.pot_x, combineY, kText,
             (pause.craft_sel >= nMat || combineHover) ? 1.0f : 0.55f);

    const float detailY = combineY + lineH() * 1.6f;
    drawItemDetail(matRows, v.reg, focus, lo.left_x, detailY, lo.content_w);

    return (v.mouse.clicked && combineHover) ? Action::Craft : Action::None;
}

// `text` cut to fit `maxW`, with an ellipsis when it doesn't. Cuts at a word where it can,
// so a truncated note trails off mid-thought rather than mid-word.
std::string elide(const std::string& text, float maxW)
{
    if (UIRenderer::measureText(sFont, text).width <= maxW)
        return text;
    std::string out;
    std::size_t i = 0;
    while (i < text.size())
    {
        const std::size_t sp = text.find(' ', i);
        const std::string word = text.substr(i, sp == std::string::npos ? sp : sp - i);
        std::string trial = out;
        if (!trial.empty())
            trial += ' ';
        trial += word;
        if (UIRenderer::measureText(sFont, trial + "...").width > maxW)
            break;
        out = trial;
        i = (sp == std::string::npos) ? text.size() : sp + 1;
    }
    return out.empty() ? "..." : out + "...";
}

// A notebook row: the note in his own words, indented under its dateline. The NOTE is the
// label -- a rarity word names what a thought is worth, not which one it is, and a column
// of "Uncommon / Uncommon / Rare" is unreadable. Rarity is carried by the ink the note is
// written in, not by a mark beside it: a mark with no legend is just a bullet, and at the
// commonest band its color is near-white, which is exactly what a bullet looks like. The
// cursored row gets the same warm lift as any list.
void drawNotebookRow(const growth::GrowthState& g, const notebook::Entry& e, float x, float y,
                     float rowW, float rowH, bool active)
{
    if (active)
        UIRenderer::drawRect(x, y, rowW, rowH, kCellCursor);

    const observations::Thought& t = *e.thought;
    const float textX = x + rowH * 0.5f; // indented under the dateline, as a written page is
    const std::string label = elide(t.text, rowW - (textX - x) - rowH * 0.2f);
    softText(label, textX, y + (rowH - UIRenderer::measureText(sFont, label).height) * 0.5f,
             reading_color::forReading(g, t.faculty, t.difficulty), active ? 1.0f : 0.75f);
}

// A day's heading -- the dateline a written page opens with. `day` 0 is the notebook's
// "this moment was never recorded" bucket, never a real day (the clock counts from 1).
void drawDayHeading(int day, float x, float y, float w)
{
    const std::string label = day > 0 ? "Day " + std::to_string(day) : "Day unknown";
    softText(label, x, y, kTextDim);
    const float lw = UIRenderer::measureText(sFont, label).width;
    const float ruleX = x + lw + 12.0f;
    UIRenderer::drawRect(ruleX, y + lineH() * 0.45f, std::max(0.0f, w - (ruleX - x)), 1.0f,
                         {kTextDim.r, kTextDim.g, kTextDim.b, 0.25f});
}

// The selected note, read back on the SAME page it was written on: the reading box's paper
// panel, so a thought looks like one thing whether it's landing or being looked up. Ink on
// paper here, not the screen's light-on-dark text -- the panel brings its own palette.
void drawNotebookDetail(const growth::GrowthState& g, const notebook::Entry& e,
                        const worldclock::WorldClock& clock, bool can_tell_time, float x, float y,
                        float w, float h)
{
    const observations::Thought& t = *e.thought;
    const Color hue = reading_color::forReading(g, t.faculty, t.difficulty);
    const float pad = lineH() * 0.7f;
    const screen_style::Inset in = screen_style::paperPanel(x, y, w, h, hue, pad, pad);

    // Plain text, not the screen's shadow-text: the drop shadow buys legibility over the
    // moving world, and on paper it just reads as smudged ink.
    const auto ink = [](const std::string& s, float tx, float ty, const Color& c)
    { UIRenderer::drawText(sFont, s, tx, ty, c); };

    // Faculty pinned left in its hue, rarity right -- the header the box uses, so the two
    // surfaces read as one notebook.
    ink(reading_color::facultyLabel(t.faculty), in.x, in.y, hue);
    const std::string rarity = reading_color::rarityWord(t.difficulty);
    ink(rarity, in.x + in.w - UIRenderer::measureText(sFont, rarity).width, in.y,
        reading_color::rarityColor(t.difficulty));
    float cy = in.y + lineH() * 1.1f;
    UIRenderer::drawRect(
        in.x, cy, in.w, 1.0f,
        {screen_style::kInkFaint.r, screen_style::kInkFaint.g, screen_style::kInkFaint.b, 0.45f});
    cy += lineH() * 0.5f;

    // The dateline, in faded ink. The moment is on record either way; the WATCH is what
    // lets him put an hour to it. Without one he still knows which day of the walk he was
    // on -- you can count days by sleeping -- just not what time it was.
    if (notebook::timed(e))
    {
        const std::string when = can_tell_time
                                     ? worldclock::stampAt(clock, e.at)
                                     : "Day " + std::to_string(worldclock::dayAt(clock, e.at));
        ink(when, in.x, cy, screen_style::kInkFaint);
        cy += lineH() * 1.2f;
    }

    cy = inkTextWrapped(t.text, in.x, cy, in.w, screen_style::kInkBody);
    cy += lineH() * 0.6f;
    ink("Spirit  +" + std::to_string(t.spirit_exp), in.x, cy, screen_style::kInkFaint);
}

// The Notebook tab, drawn as pages: each day gets a dateline, then the notes he wrote under
// it. Only his own -- what he hasn't thought has no line here. List on the LEFT, the full
// note on the RIGHT, the same shape as the Satchel; hovering a note selects it.
void renderNotebook(PauseState& pause, const growth::GrowthState& g, const Content& content,
                    const Mouse& mouse, float cx, float y, Canvas canvas)
{
    const notebook::Record& rec = content.notebook;
    const observations::State& obs = content.observations;
    const std::vector<notebook::Day> days = notebook::byDay(rec, obs, content.clock);
    const float contentW = contentBandW(canvas);
    const float leftX = cx - contentW * 0.5f;
    const float colGap = contentW * 0.08f;
    const float listW = (contentW - colGap) * 0.5f;

    int noteCount = 0;
    for (const auto& d : days)
        noteCount += static_cast<int>(d.entries.size());
    if (noteCount == 0)
    {
        softText("Nothing yet", leftX, y, kTextDim);
        return;
    }
    pause.notebook_sel = std::clamp(pause.notebook_sel, 0, noteCount - 1);

    // Where each note's row sits, walked once. Datelines are chrome -- never selectable -- so
    // they don't consume a cursor index, which makes the note index and the drawn geometry
    // disagree unless one walk produces both. This is that walk: the hit-test, the draw, and
    // the detail panel all read it.
    struct Row
    {
        const notebook::Entry* entry;
        float y;
    };
    std::vector<Row> rows;
    std::vector<std::pair<int, float>> headings; // day -> its y
    const float rowH = listRowH();
    float rowY = y;
    for (const auto& day : days)
    {
        headings.emplace_back(day.day, rowY);
        rowY += lineH() * 1.3f;
        for (const auto& e : day.entries)
        {
            rows.push_back(Row{&e, rowY});
            rowY += rowH;
        }
        rowY += lineH() * 0.5f; // breathing room before the next dateline
    }

    // Hover BEFORE drawing, so the highlight and the detail agree with the mouse this frame.
    for (int i = 0; i < static_cast<int>(rows.size()); ++i)
        if (engine::ui::pointInRect(mouse.x, mouse.y, leftX, rows[static_cast<std::size_t>(i)].y,
                                    listW, rowH))
            pause.notebook_sel = i;

    for (const auto& [day, hy] : headings)
        drawDayHeading(day, leftX, hy, listW);
    for (int i = 0; i < static_cast<int>(rows.size()); ++i)
    {
        const Row& r = rows[static_cast<std::size_t>(i)];
        drawNotebookRow(g, *r.entry, leftX, r.y, listW, rowH, i == pause.notebook_sel);
    }
    // The page fills the list's fixed region, so it stays one steady sheet rather than
    // resizing to whatever note is selected.
    // Whether he can read an hour off a note is the WATCH's business -- the moment itself is
    // always on record (see noteLanded).
    const bool canTellTime = inventory::has(content.satchel, "watch");
    drawNotebookDetail(g, *rows[static_cast<std::size_t>(pause.notebook_sel)].entry, content.clock,
                       canTellTime, leftX + listW + colGap, y, contentW - listW - colGap,
                       listRegionH());
}

// One "Label   Keys" control line, label right-aligned to a shared column so the
// key column lines up. Centered as a pair around cx.
void controlLine(const std::string& label, const std::string& keys, float cx, float y)
{
    const float colGap = 24.0f;
    const auto lblSz = UIRenderer::measureText(sFont, label);
    const float keysX = cx + colGap * 0.5f;
    softText(label, keysX - colGap - lblSz.width, y, kTextDim);
    softText(keys, keysX, y, kText);
}

// Draw one centered, selectable menu item (a System-tab row). The active row
// (keyboard-selected or hovered) is drawn bright; inactive rows are dimmed. No
// box or wash -- brightness alone marks the selection. Returns true if the mouse
// is over it. The hit rect spans a generous row for a comfortable hover area.
bool menuItem(const std::string& label, float cx, float y, bool selected, const Mouse& mouse)
{
    const float rowW = 260.0f;
    const float rowH = lineH();
    const float x = cx - rowW * 0.5f;
    const bool hovered = engine::ui::pointInRect(mouse.x, mouse.y, x, y - 4.0f, rowW, rowH);
    softTextCentered(label, cx, y, kText, (selected || hovered) ? 1.0f : 0.55f);
    return hovered;
}

// The System tab: a small menu. Draws every item and returns the one the mouse is over
// (-1 = none), so the caller resolves a click by index rather than by a bool per item --
// which is what stops a new entry from needing a new out-param.
//
// Leaving sits above Quit: they're the same act at different distances (leave the walk /
// leave the game), and both keep the walk.
int renderSystem(float cx, float y, int sel, const Mouse& mouse)
{
    constexpr const char* kLabels[kSysItemCount] = {"Controls", "Settings", "Leave to title",
                                                    "Quit to desktop"};
    int hovered = -1;
    for (int i = 0; i < kSysItemCount; ++i)
    {
        if (menuItem(kLabels[i], cx, y, sel == i, mouse))
            hovered = i;
        y += lineH() * 1.4f;
    }
    return hovered;
}

// The Controls sub-view: the bindings reference, shown in the content area below
// the (still-visible) tabs. F pops back to the System menu (handled in step).
void renderControlsView(float cx, float y)
{
    controlLine("Move", "W A S D", cx, y);
    y += lineH();
    controlLine("Interact", "Space", cx, y);
    y += lineH();
    controlLine("Back / Pause", "F", cx, y);
    y += lineH();
    controlLine("Tabs", "A / D", cx, y);
}

} // namespace

void init(FontHandle font)
{
    sFont = font;
}

// Keyboard handling for the System tab's menu (Controls / Quit). W/S establish a
// selection (from "none" (-1): down picks the first item, up the last; then it
// wraps); confirm acts only once something is selected. Returns Quit if Quit was
// confirmed, else None (Controls confirm pushes its sub-view as a side effect).
// The Craft tab's item menu: W/S move over the material rows + a trailing Combine row; Space
// toggles a material into the attempt, or (on Combine) commits the attempt (returns Craft --
// the caller runs the actual craft, which needs the recipe registry). `materials` is the row
// list, in the same order render draws. Selecting a material clears the last result.
Action stepCraft(PauseState& pause, const std::vector<CraftMaterial>& materials, bool up, bool down,
                 bool confirm)
{
    const int rows = static_cast<int>(materials.size()) + 1; // materials + Combine
    if (down)
        pause.craft_sel = (pause.craft_sel + 1) % rows;
    else if (up)
        pause.craft_sel = (pause.craft_sel - 1 + rows) % rows;

    if (confirm)
    {
        if (pause.craft_sel == static_cast<int>(materials.size()))
            return Action::Craft; // Combine row -> attempt (caller enacts)
        // Space throws one more of the highlighted material in, capped at how many are carried.
        const CraftMaterial& m = materials[static_cast<std::size_t>(pause.craft_sel)];
        if (pause.craft_selected[m.id] < m.carried)
            ++pause.craft_selected[m.id];
        else
            pause.craft_selected.erase(m.id); // full stack in -> next Space empties it (a reset)
    }
    return Action::None;
}

// Commit one System item by index. The ONE place an item's meaning lives, so the keys and
// the mouse can't disagree -- and so adding an entry can't accidentally inherit another's
// behaviour (an `else -> Quit` would hand every new item the quit action).
Action commitSystemItem(PauseState& pause, int item)
{
    switch (item)
    {
    case kSysControls:
        pause.view_stack.push_back(PauseState::View::Controls);
        return Action::None;
    case kSysSettings:
        // Not a view_stack push: settings is a PHASE (the title opens the same screen), and
        // the page doesn't own phases. The caller takes it from here.
        return Action::Settings;
    case kSysLeave:
        return Action::Leave;
    case kSysQuit:
        return Action::Quit;
    default:
        return Action::None;
    }
}

Action stepSystemMenu(PauseState& pause, bool up, bool down, bool confirm)
{
    if (down)
        pause.system_sel =
            (pause.system_sel < 0) ? kSysControls : (pause.system_sel + 1) % kSysItemCount;
    else if (up)
        pause.system_sel = (pause.system_sel < 0)
                               ? (kSysItemCount - 1)
                               : (pause.system_sel - 1 + kSysItemCount) % kSysItemCount;

    if (confirm && pause.system_sel >= 0)
        return commitSystemItem(pause, pause.system_sel);
    return Action::None;
}

// How far W/S move a list cursor this frame (0 when neither is down).
int listStep(bool up, bool down)
{
    if (down)
        return 1;
    return up ? -1 : 0;
}

// Hand the keys to whatever tab is showing. System = its item menu; Craft = material select +
// Combine; Satchel and Notebook = read-only list cursors (W/S walk the rows for the detail
// panel, clamped at render time against the live list). Self is a plain readout -- no cursor.
Action stepTab(PauseState& pause, const std::vector<CraftMaterial>& craftMats, bool up, bool down,
               bool confirm)
{
    switch (pause.tab)
    {
    case PauseState::Tab::System:
        return stepSystemMenu(pause, up, down, confirm);
    case PauseState::Tab::Craft:
        return stepCraft(pause, craftMats, up, down, confirm);
    case PauseState::Tab::Satchel:
        pause.satchel_sel += listStep(up, down);
        break;
    case PauseState::Tab::Notebook:
        pause.notebook_sel += listStep(up, down);
        break;
    case PauseState::Tab::Self:
        break;
    }
    return Action::None;
}

Action step(PauseState& pause, bool toggle, bool left, bool right, bool up, bool down, bool confirm,
            const std::vector<CraftMaterial>& craftMats)
{
    if (!pause.open)
    {
        if (toggle)
        {
            pause.open = true;
            pause.tab = PauseState::Tab::Self;
            pause.view_stack.clear();
            pause.system_sel = -1; // nothing highlighted until hover / W-S
        }
        return Action::None;
    }

    // In a sub-view, Back (F) pops one level rather than closing the page.
    if (!pause.view_stack.empty())
    {
        if (toggle)
            pause.view_stack.pop_back();
        return Action::None;
    }

    // On the tab strip, Back (F) closes the page.
    if (toggle)
    {
        pause.open = false;
        return Action::Resume;
    }

    // Left/Right page through the tabs (wrapping, D-pad style).
    if (left || right)
    {
        const int dir = right ? 1 : -1;
        const int next = (static_cast<int>(pause.tab) + dir + kTabCount) % kTabCount;
        pause.tab = static_cast<PauseState::Tab>(next);
    }

    return stepTab(pause, craftMats, up, down, confirm);
}

// Draw the always-visible tab strip (centered near the top) and handle clicks:
// clicking a tab switches to it and pops any open sub-view. A layout pre-pass
// gives each tab's x/width so the mouse hit-tests the same rects that are drawn.
void renderTabStrip(PauseState& pause, float cx, float tabY, const Mouse& mouse)
{
    // Order must match PauseState::Tab: Self, Satchel, Craft, Notebook, System.
    const char* labels[kTabCount] = {"Self", "Satchel", "Craft", "Notebook", "System"};
    const float tabH = lineH() + 10.0f;
    const float tabGap = 6.0f;
    float widths[kTabCount];
    float rowW = 0.0f;
    for (int i = 0; i < kTabCount; ++i)
    {
        widths[i] = tabWidth(labels[i]);
        rowW += widths[i] + (i > 0 ? tabGap : 0.0f);
    }
    const float rowX = cx - rowW * 0.5f;

    // Baseline under the whole row (the tab strip's seam with the content).
    UIRenderer::drawRect(rowX - 40.0f, tabY + tabH, rowW + 80.0f, 1.0f, kTabBaseline);

    float tx = rowX;
    for (int i = 0; i < kTabCount; ++i)
    {
        const bool active = static_cast<int>(pause.tab) == i;
        const bool hovered = engine::ui::pointInRect(mouse.x, mouse.y, tx, tabY, widths[i], tabH);
        drawTab(labels[i], tx, tabY, widths[i], tabH, active, hovered);
        if (hovered && mouse.clicked)
        {
            pause.tab = static_cast<PauseState::Tab>(i);
            pause.view_stack.clear();
        }
        tx += widths[i] + tabGap;
    }
}

// Draw the active tab's content (or a pushed sub-view) in the content area below
// the tabs, and resolve clicks on the System tab's items. Returns Quit if Quit
// was clicked, else None (a Controls click pushes its sub-view).
Action renderTabContent(PauseState& pause, const growth::GrowthState& growth,
                        const Content& content, const IconResolver& icon, float cx, float contentY,
                        const Mouse& mouse, Canvas canvas)
{
    if (!pause.view_stack.empty())
    {
        switch (pause.view_stack.back())
        {
        case PauseState::View::Controls:
            renderControlsView(cx, contentY);
            break;
        }
        return Action::None;
    }

    switch (pause.tab)
    {
    case PauseState::Tab::Self:
        renderSelf(growth, cx, contentY);
        break;
    case PauseState::Tab::Satchel:
    {
        const std::vector<GridItem> items = satchelGrid(content.satchel, content.items);
        const SatchelView view{items, content.items, icon, mouse};
        renderSatchel(pause, view, cx, contentY, canvas);
        break;
    }
    case PauseState::Tab::Craft:
    {
        const std::vector<CraftMaterial> mats = craftMaterials(content.satchel, content.items);
        const CraftView view{content.satchel, content.items, icon, mats, mouse};
        return renderCraft(pause, view, cx, contentY, canvas);
    }
    case PauseState::Tab::Notebook:
        renderNotebook(pause, growth, content, mouse, cx, contentY, canvas);
        break;
    case PauseState::Tab::System:
    {
        const int hovered = renderSystem(cx, contentY, pause.system_sel, mouse);
        if (hovered >= 0)
            pause.system_sel = hovered; // the hand moves the cursor, as everywhere else
        if (mouse.clicked && hovered >= 0)
            return commitSystemItem(pause, hovered); // the same meaning the keys commit
        break;
    }
    }
    return Action::None;
}

Action render(PauseState& pause, const growth::GrowthState& growth, const Content& content,
              const Mouse& mouse, const IconResolver& icon, int windowW, int windowH)
{
    if (!pause.open || sFont < 0)
        return Action::None;

    const float ww = static_cast<float>(windowW);
    const float wh = static_cast<float>(windowH);
    const float cx = ww * 0.5f;

    // Soft darkening of the frozen world (the whole overlay -- no panel box).
    UIRenderer::drawRect(0.0f, 0.0f, ww, wh, kOverlay);

    // The tab strip always stays visible; sub-views render in the content area
    // below it, never replacing the tabs.
    renderTabStrip(pause, cx, wh * 0.12f, mouse);
    return renderTabContent(pause, growth, content, icon, cx, wh * 0.24f, mouse,
                            Canvas{windowW, windowH});
}

} // namespace pause_page
