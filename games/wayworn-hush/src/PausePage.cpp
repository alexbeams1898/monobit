#include "PausePage.h"

#include "FontManager.h"
#include "Inventory.h"
#include "Notebook.h"
#include "ReadingColor.h"
#include "ScreenInput.h"
#include "UIRenderer.h"

#include <string>

namespace pause_page
{
namespace
{
FontHandle sFont = -1;

constexpr int kTabCount = 5; // Self, Noticed, Satchel, Notebook, System

// System-tab menu items.
constexpr int kSysControls = 0;
constexpr int kSysQuit = 1;
constexpr int kSysItemCount = 2;

// Wayworn's minimal register: muted shadow-text over a soft darkening of the
// frozen world. Colors mirror the thought box (AESTHETIC.md).
constexpr Color kOverlay{0.04f, 0.05f, 0.06f, 0.55f};
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

void renderNoticed(const growth::GrowthState& g, const observations::State& o, float cx, float y)
{
    // What's been noticed: the objective observations you've reached (the deepest
    // tier text per spot, plain), then the thoughts you've had (colored by
    // faculty + rarity). The full thought-notebook split is a later slice; for now
    // both share this list. Empty stays quiet.
    bool any = false;
    for (const auto& ob : o.observables)
    {
        const auto it = o.observed_tier.find(ob.id);
        if (it == o.observed_tier.end())
            continue;
        const int tier = it->second; // 1-based deepest tier reached
        if (tier >= 1 && tier <= static_cast<int>(ob.tiers.size()))
        {
            softTextCentered(ob.tiers[static_cast<std::size_t>(tier - 1)].text, cx, y, kText);
            y += lineH() * 1.2f;
            any = true;
        }
    }
    for (const auto& r : o.thoughts)
    {
        if (o.fired.count(r.id) == 0)
            continue;
        const Color hue = reading_color::forReading(g, r.faculty, r.difficulty);
        // Label: faculty (its hue) + rarity word (its own loot color).
        if (r.difficulty > 0 && !r.faculty.empty())
        {
            const std::string faculty = reading_color::facultyLabel(r.faculty);
            const std::string rarity = reading_color::rarityWord(r.difficulty);
            const float gap = 24.0f;
            const float fw = UIRenderer::measureText(sFont, faculty).width;
            const float startX =
                cx - (fw + gap + UIRenderer::measureText(sFont, rarity).width) * 0.5f;
            softText(faculty, startX, y, hue);
            softText(rarity, startX + fw + gap, y, reading_color::rarityColor(r.difficulty));
            y += lineH() * 0.85f;
        }
        softTextCentered(r.text, cx, y, hue);
        y += lineH() * 1.3f;
        any = true;
    }
    if (!any)
        softTextCentered("None", cx, y, kTextDim);
}

// The Satchel tab: what the pilgrim carries, grouped by category (key items first
// -- notebook, watch -- then keepsakes, then practical). Each line is the item
// name (rarity-colored), with a "x N" suffix for a stack.
void renderSatchel(const inventory::Satchel& sat, const inventory::Registry& reg, float cx, float y)
{
    // Category order + heading; key items lead (they're the meaningful ones).
    const struct
    {
        inventory::Category cat;
        const char* heading;
    } sections[] = {{inventory::Category::KeyItem, "Carried"},
                    {inventory::Category::Keepsake, "Kept"},
                    {inventory::Category::Practical, "Gathered"}};

    bool any = false;
    for (const auto& sec : sections)
    {
        bool headingDrawn = false;
        for (const auto& e : sat.items)
        {
            const inventory::ItemDef* def = reg.find(e.id);
            const inventory::Category cat = def ? def->category : inventory::Category::Keepsake;
            if (cat != sec.cat)
                continue;
            if (!headingDrawn)
            {
                softTextCentered(sec.heading, cx, y, kTextDim);
                y += lineH() * 1.1f;
                headingDrawn = true;
            }
            std::string label = def ? def->name : e.id;
            if (e.quantity > 1)
                label += "  x" + std::to_string(e.quantity);
            const Color c = def ? reading_color::rarityColor(def->rarity) : kText;
            softTextCentered(label, cx, y, c);
            y += lineH();
            any = true;
        }
        if (headingDrawn)
            y += lineH() * 0.5f;
    }
    if (!any)
        softTextCentered("Nothing yet", cx, y, kTextDim);
}

// The Notebook tab: the dated record of readings, grouped by day (undated last).
// Each day gets a "~ Day N ~" header; observations read plain, thoughts in their
// faculty hue -- the same register split as the reading box.
void renderNotebook(const growth::GrowthState& g, const notebook::Record& rec, float cx, float y)
{
    const auto groups = notebook::groupByDay(rec);
    if (groups.empty())
    {
        softTextCentered("Empty", cx, y, kTextDim);
        return;
    }
    for (const auto& grp : groups)
    {
        const std::string header = grp.day > 0 ? "~ Day " + std::to_string(grp.day) + " ~" : "~ ~";
        softTextCentered(header, cx, y, kTextDim);
        y += lineH() * 1.2f;
        for (const auto& e : grp.entries)
        {
            const bool thought = e.kind == observations::LineKind::Thought;
            const Color c = thought ? reading_color::forReading(g, e.faculty, e.difficulty) : kText;
            softTextCentered(e.text, cx, y, c);
            y += lineH();
        }
        y += lineH() * 0.6f;
    }
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

// The System tab: a small menu (Controls / Quit). Draws both items; sets
// `quit_hovered`/`controls_hovered` for the caller to resolve clicks.
void renderSystem(float cx, float y, int sel, const Mouse& mouse, bool& controls_hovered,
                  bool& quit_hovered)
{
    controls_hovered = menuItem("Controls", cx, y, sel == kSysControls, mouse);
    y += lineH() * 1.4f;
    quit_hovered = menuItem("Quit", cx, y, sel == kSysQuit, mouse);
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
    {
        if (pause.system_sel == kSysControls)
            pause.view_stack.push_back(PauseState::View::Controls);
        else
            return Action::Quit;
    }
    return Action::None;
}

Action step(PauseState& pause, bool toggle, bool left, bool right, bool up, bool down, bool confirm)
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

    // Only the System tab is interactive; the other tabs are read-only.
    if (pause.tab == PauseState::Tab::System)
        return stepSystemMenu(pause, up, down, confirm);

    return Action::None;
}

// Draw the always-visible tab strip (centered near the top) and handle clicks:
// clicking a tab switches to it and pops any open sub-view. A layout pre-pass
// gives each tab's x/width so the mouse hit-tests the same rects that are drawn.
void renderTabStrip(PauseState& pause, float cx, float tabY, const Mouse& mouse)
{
    // Order must match PauseState::Tab: Self, Noticed, Satchel, Notebook, System.
    const char* labels[kTabCount] = {"Self", "Noticed", "Satchel", "Notebook", "System"};
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
                        const Content& content, float cx, float contentY, const Mouse& mouse)
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
    case PauseState::Tab::Noticed:
        renderNoticed(growth, content.observations, cx, contentY);
        break;
    case PauseState::Tab::Satchel:
        renderSatchel(content.satchel, content.items, cx, contentY);
        break;
    case PauseState::Tab::Notebook:
        renderNotebook(growth, content.notebook, cx, contentY);
        break;
    case PauseState::Tab::System:
    {
        bool controlsHovered = false;
        bool quitHovered = false;
        renderSystem(cx, contentY, pause.system_sel, mouse, controlsHovered, quitHovered);
        if (mouse.clicked && controlsHovered)
            pause.view_stack.push_back(PauseState::View::Controls);
        else if (mouse.clicked && quitHovered)
            return Action::Quit;
        break;
    }
    }
    return Action::None;
}

Action render(PauseState& pause, const growth::GrowthState& growth, const Content& content,
              const Mouse& mouse, int windowW, int windowH)
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
    renderTabStrip(pause, cx, wh * 0.15f, mouse);
    return renderTabContent(pause, growth, content, cx, wh * 0.30f, mouse);
}

} // namespace pause_page
