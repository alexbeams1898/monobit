#include "ThoughtBox.h"

#include "FontManager.h"
#include "Notify.h"
#include "ReadingColor.h"
#include "UIRenderer.h"
#include "systems/AudioSystem.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace thought_box
{
namespace
{
FontHandle sFont = -1;        // body (reading text)
FontHandle sHeadingFont = -1; // smaller label line (faculty + rarity)
Config sCfg;

constexpr float kBoxTopFrac = 0.70f; // box top edge as a fraction of window height

// What the box is currently showing.
enum class ItemKind
{
    None,
    Line, // a reading / thought / deed-result (typewriter, Souls-like dismiss)
    Menu  // the action deed list for a spot (W/S select, Space confirm, back)
};
ItemKind sItem = ItemKind::None;

// Animation phases for a Line. After Typing the box sits in Done until the player
// dismisses it (Souls-like); it never auto-fades. Menus skip straight to Done.
enum class Phase
{
    None,
    DropIn,
    Typing,
    Done,
    FadeOut
};

// --- Line state ---
observations::PendingLine sLine; // reading on screen (empty text = none)
Phase sPhase = Phase::None;
float sPhaseT = 0.0f;              // seconds elapsed in the current phase
int sRevealed = 0;                 // characters of the body revealed so far
int sLastBlip = 0;                 // revealed-count at the last blip (rate-limits the tick)
std::vector<std::string> sWrapped; // body wrapped into lines (built when a line loads)

// --- Menu state ---
struct Option
{
    std::string id;    // action id (for takeAction)
    std::string label; // shown text
};
std::string sMenuSpot;         // observable id the menu belongs to
std::vector<Option> sMenuOpts; // current offered deeds
int sMenuSel = 0;              // highlighted option
bool sMenuQueued = false;      // a menu wants to open once state.pending is empty
                               // (fresh after observe, or re-show after a deed's result)
// Last-rendered menu geometry, stashed so mouse hit-testing matches exactly what
// was drawn (no recompute drift).
float sMenuX = 0.0f;
float sMenuY = 0.0f;
float sMenuW = 0.0f;
float sMenuRowH = 0.0f;

// --- helpers ---------------------------------------------------------------

std::vector<std::string> wrapText(const std::string& text, float max_width)
{
    std::vector<std::string> lines;
    std::string line;
    std::size_t i = 0;
    while (i <= text.size())
    {
        const std::size_t start = i;
        while (i < text.size() && text[i] != ' ')
            ++i;
        const std::string word = text.substr(start, i - start);
        ++i; // skip the space

        std::string candidate = line;
        if (!candidate.empty())
            candidate += ' ';
        candidate += word;
        if (UIRenderer::measureText(sFont, candidate).width > max_width && !line.empty())
        {
            lines.push_back(line);
            line = word;
        }
        else
        {
            line = candidate;
        }
    }
    if (!line.empty())
        lines.push_back(line);
    return lines;
}

int totalChars()
{
    int n = 0;
    for (const auto& l : sWrapped)
        n += static_cast<int>(l.size());
    return n;
}

// Play the drop-in sound, pitch/volume scaled by rarity, with a shimmer layer
// for rare+ readings (mirrors the studio pickup-SFX pattern).
void playAppearSfx(int difficulty)
{
    const float tier = static_cast<float>(std::max(0, difficulty - 1)); // 0..4
    const float pitch = 1.0f + tier * 0.06f;
    const float vol = 0.7f * (1.0f + tier * 0.12f);
    AudioSystem::playSfx(sCfg.appear_sound, vol, pitch);
    if (difficulty >= 4) // Rare / Legendary get a brighter shimmer layer
        AudioSystem::playSfx(sCfg.appear_sound, vol * 0.5f, pitch * 1.6f);
}

void loadLine(observations::PendingLine line, int windowW)
{
    sItem = ItemKind::Line;
    sLine = std::move(line);
    sPhase = Phase::DropIn;
    sPhaseT = 0.0f;
    sRevealed = 0;
    sLastBlip = 0;
    const float maxW = static_cast<float>(windowW) * sCfg.max_width_frac;
    sWrapped = wrapText(sLine.text, maxW);
    playAppearSfx(sLine.difficulty);
    // The reward toast lands WITH the line on screen (not when the engine queued
    // it seconds earlier), so "+N Spirit" reads alongside the thought that earned it.
    if (sLine.spirit_exp > 0)
        notify::push("+" + std::to_string(sLine.spirit_exp) + " Spirit",
                     {0.98f, 0.90f, 0.55f, 1.0f});
}

// Sentinel option id: always the last entry, closes the menu (equivalent to F).
const char* const kLeaveId = "__leave__";

// Fill the menu with the spot's currently-offered deeds plus a trailing "Leave".
// Returns false (no menu) if the spot has no real deeds -- Leave alone is not a
// menu worth opening.
bool buildMenu(const observations::State& state, const growth::GrowthState& growth,
               const std::string& spot)
{
    sMenuOpts.clear();
    for (const auto* a : observations::availableActions(state, growth, spot))
        sMenuOpts.push_back(Option{a->id, a->label});
    if (sMenuOpts.empty())
        return false;
    sMenuOpts.push_back(Option{kLeaveId, "Leave"});
    return true;
}

void openMenu()
{
    sItem = ItemKind::Menu;
    sPhase = Phase::DropIn;
    sPhaseT = 0.0f;
    sMenuSel = 0;
    sMenuQueued = false; // consumed -- the menu is now open
    AudioSystem::playSfx(sCfg.appear_sound, 0.6f, 1.0f);
}

// Advance the typewriter; play a blip every `blip_every` newly-revealed chars.
void advanceTyping(float dt)
{
    const int target =
        std::min(totalChars(), sRevealed + static_cast<int>(std::ceil(sCfg.chars_per_sec * dt)));
    if (target > sRevealed)
    {
        sRevealed = target;
        if (sRevealed - sLastBlip >= sCfg.blip_every)
        {
            sLastBlip = sRevealed;
            AudioSystem::playSfx(sCfg.blip_sound, 0.35f, 1.0f);
        }
    }
    if (sRevealed >= totalChars())
    {
        sPhase = Phase::Done; // wait for the player to dismiss
        sPhaseT = 0.0f;
    }
}

// --- draw helpers ----------------------------------------------------------

void drawBorder(float x, float y, float w, float h, const Color& c)
{
    constexpr float t = 2.0f;
    UIRenderer::drawRect(x, y, w, t, c);
    UIRenderer::drawRect(x, y + h - t, w, t, c);
    UIRenderer::drawRect(x, y, t, h, c);
    UIRenderer::drawRect(x + w - t, y, t, h, c);
}

// Shared box geometry + chrome for both the Line and Menu panels: centered,
// anchored at the box-top, sized to its content (min width), padded. Draws the
// dark backing + colored border, and returns the top-left of the padded content.
constexpr float kPadX = 28.0f;
constexpr float kPadY = 16.0f;
struct PanelOrigin
{
    float x, y, w;
};
PanelOrigin drawPanel(float ww, float wh, float contentW, float contentH, float alpha,
                      float dropOffset, float bgAlpha, const Color& border)
{
    const float boxW = std::max(contentW, 240.0f) + kPadX * 2.0f;
    const float boxH = contentH + kPadY * 2.0f;
    const float boxX = (ww - boxW) * 0.5f;
    const float boxY = wh * kBoxTopFrac + dropOffset;
    UIRenderer::drawRect(boxX, boxY, boxW, boxH, {0.05f, 0.06f, 0.07f, bgAlpha * alpha});
    drawBorder(boxX, boxY, boxW, boxH, {border.r, border.g, border.b, 0.55f * alpha});
    return {boxX + kPadX, boxY + kPadY, boxW};
}

void outlinedText(FontHandle font, const std::string& s, float x, float y, const Color& fill,
                  float alpha)
{
    const Color outline{0.0f, 0.0f, 0.0f, 0.7f * alpha};
    for (const float dx : {-1.0f, 1.0f})
        for (const float dy : {-1.0f, 1.0f})
            UIRenderer::drawText(font, s, x + dx, y + dy, outline);
    UIRenderer::drawText(font, s, x, y, {fill.r, fill.g, fill.b, alpha});
}

std::vector<std::string> revealedLines()
{
    std::vector<std::string> out;
    int budget = sRevealed;
    for (const auto& full : sWrapped)
    {
        if (budget <= 0)
            break;
        const int take = std::min(budget, static_cast<int>(full.size()));
        out.push_back(full.substr(0, static_cast<std::size_t>(take)));
        budget -= static_cast<int>(full.size());
    }
    return out;
}

// The entrance/exit alpha + drop offset, shared by Line and Menu.
void entranceAnim(float& alpha, float& dropOffset)
{
    alpha = 1.0f;
    dropOffset = 0.0f;
    if (sPhase == Phase::DropIn)
    {
        const float t = std::clamp(sPhaseT / sCfg.drop_in_secs, 0.0f, 1.0f);
        alpha = t;
        dropOffset = (1.0f - t) * 24.0f;
    }
    else if (sPhase == Phase::FadeOut)
    {
        alpha = std::clamp(1.0f - sPhaseT / sCfg.fade_out_secs, 0.0f, 1.0f);
    }
}
} // namespace

void init(FontHandle body_font, FontHandle heading_font, const Config& config)
{
    sFont = body_font;
    sHeadingFont = heading_font;
    sCfg = config;
}

float boxTopFrac()
{
    return kBoxTopFrac;
}

void update(observations::State& state, const growth::GrowthState& growth, float dt)
{
    if (sItem == ItemKind::None)
    {
        // Reading / result lines ALWAYS drain first -- the menu waits until the
        // player has read everything queued (so a spot's readings show before its
        // deed menu, and a deed's result_text shows before the menu re-opens).
        if (!state.pending.empty())
        {
            observations::PendingLine next = state.pending.front();
            state.pending.pop_front();
            loadLine(std::move(next), 1536);
            return;
        }
        // Pending is empty: if a menu is queued (fresh or re-show after a deed),
        // open it with the spot's currently-offered actions.
        if (sMenuQueued && buildMenu(state, growth, sMenuSpot))
        {
            openMenu();
            return;
        }
        sMenuQueued = false;
        return;
    }

    sPhaseT += dt;
    switch (sPhase)
    {
    case Phase::DropIn:
        if (sPhaseT >= sCfg.drop_in_secs)
        {
            sPhase = (sItem == ItemKind::Menu) ? Phase::Done : Phase::Typing;
            sPhaseT = 0.0f;
        }
        break;
    case Phase::Typing:
        advanceTyping(dt);
        break;
    case Phase::Done:
        break; // holds; dismissed by confirm()/back()
    case Phase::FadeOut:
        if (sPhaseT >= sCfg.fade_out_secs)
        {
            sPhase = Phase::None;
            sItem = ItemKind::None;
            sLine = {};
        }
        break;
    case Phase::None:
        break;
    }
}

bool active()
{
    return sItem != ItemKind::None;
}

bool menuActive()
{
    return sItem == ItemKind::Menu && sPhase != Phase::FadeOut;
}

void pushActionMenu(observations::State& state, const growth::GrowthState& growth,
                    const std::string& spot)
{
    // Queue the menu: update() opens it once all pending reading lines are read.
    // buildMenu here is just a has-any check; update() rebuilds at open time.
    sMenuSpot = spot;
    sMenuQueued = buildMenu(state, growth, spot);
}

void dropQueuedMenuIfLeft(const std::string& facedSpot)
{
    // The action menu is bound to being AT the spot: if it's queued (waiting for
    // the reading/thoughts to be read) but the player has walked off / faces a
    // different spot, drop it -- the menu shouldn't chase you. An OPEN menu is
    // modal (movement frozen) so this only fires in the pre-open window; the
    // observation's own reading + thoughts are unaffected and play out.
    if (sMenuQueued && sItem != ItemKind::Menu && facedSpot != sMenuSpot)
        sMenuQueued = false;
}

int confirm(observations::State& state, const growth::GrowthState& growth,
            const observations::RollRng& rng)
{
    if (sItem == ItemKind::Line)
    {
        if (sPhase == Phase::Typing)
        {
            sRevealed = totalChars(); // reveal all
            sPhase = Phase::Done;
            sPhaseT = 0.0f;
        }
        else if (sPhase == Phase::Done)
        {
            sPhase = Phase::FadeOut; // dismiss
            sPhaseT = 0.0f;
        }
        return 0;
    }
    if (sItem == ItemKind::Menu && sPhase == Phase::Done)
    {
        if (sMenuSel < 0 || sMenuSel >= static_cast<int>(sMenuOpts.size()))
            return 0;
        const std::string actionId = sMenuOpts[static_cast<std::size_t>(sMenuSel)].id;
        if (actionId == kLeaveId)
        {
            back(); // the trailing "Leave" option closes the menu
            return 0;
        }
        // Close the menu; the deed's result_text enqueues as a Line. After it is
        // read, update() re-opens the menu with the remaining offered deeds. Return
        // the EXP the deed earned (a thought it fires) so the caller banks it.
        sItem = ItemKind::None;
        sPhase = Phase::None;
        const observations::ObserveResult r =
            observations::takeAction(state, growth, sMenuSpot, actionId, rng);
        sMenuQueued = true; // re-open after the result line (+ any fired thought) is read
        return r.earned;
    }
    return 0;
}

void moveUp()
{
    if (menuActive() && sPhase == Phase::Done && !sMenuOpts.empty())
        sMenuSel = (sMenuSel + static_cast<int>(sMenuOpts.size()) - 1) %
                   static_cast<int>(sMenuOpts.size());
}

void moveDown()
{
    if (menuActive() && sPhase == Phase::Done && !sMenuOpts.empty())
        sMenuSel = (sMenuSel + 1) % static_cast<int>(sMenuOpts.size());
}

int menuMouse(observations::State& state, const growth::GrowthState& growth,
              const observations::RollRng& rng, float mx, float my, bool clicked)
{
    if (!menuActive() || sPhase != Phase::Done || sMenuOpts.empty())
        return 0;
    // Which option row is the cursor over? (Rows are stacked from sMenuY.)
    if (mx < sMenuX || mx > sMenuX + sMenuW)
        return 0;
    const int row = static_cast<int>((my - sMenuY) / sMenuRowH);
    if (row < 0 || row >= static_cast<int>(sMenuOpts.size()))
        return 0;
    sMenuSel = row;                                   // hover highlights
    return clicked ? confirm(state, growth, rng) : 0; // click confirms + banks EXP
}

void back()
{
    if (sItem == ItemKind::Menu)
    {
        sMenuQueued = false; // don't re-open; leaving the spot
        sPhase = Phase::FadeOut;
        sPhaseT = 0.0f;
    }
}

// --- render ----------------------------------------------------------------
namespace
{
void renderLine(const growth::GrowthState& growth, float ww, float wh, float alpha,
                float dropOffset)
{
    const Color hue = reading_color::forReading(growth, sLine.faculty, sLine.difficulty, 1.0f);
    // A subjective thought is faculty-hued + rarity-styled; a plain observation is
    // neutral. The header (faculty . rarity . New) is shown only for a new thought.
    const bool isThought = sLine.kind == observations::LineKind::Thought;
    const bool showHeader = sLine.is_new && isThought;

    const float headingH = static_cast<float>(FontManager::lineHeight(sHeadingFont));
    const float bodyLineH = static_cast<float>(FontManager::lineHeight(sFont));
    const float headingGap = 12.0f;
    const float labelBlock = showHeader ? headingH + headingGap : 0.0f;

    float bodyW = 0.0f;
    for (const auto& l : sWrapped)
        bodyW = std::max(bodyW, UIRenderer::measureText(sFont, l).width);
    const float bodyH = static_cast<float>(sWrapped.size()) * bodyLineH;

    const PanelOrigin p =
        drawPanel(ww, wh, bodyW, bodyH + labelBlock, alpha, dropOffset, 0.85f, hue);
    const float contentX = p.x;
    const float boxX = p.x - kPadX;

    float cursorY = p.y;
    if (showHeader)
    {
        const std::string faculty = reading_color::facultyLabel(sLine.faculty);
        UIRenderer::drawText(sHeadingFont, faculty, contentX, cursorY,
                             {hue.r, hue.g, hue.b, alpha});
        const float rx = contentX + UIRenderer::measureText(sHeadingFont, faculty).width + 20.0f;
        outlinedText(sHeadingFont, reading_color::rarityWord(sLine.difficulty), rx, cursorY,
                     reading_color::rarityColor(sLine.difficulty, alpha), alpha);
        if (sLine.is_new)
        {
            const float tagW = UIRenderer::measureText(sHeadingFont, "New").width;
            outlinedText(sHeadingFont, "New", boxX + p.w - kPadX - tagW, cursorY,
                         {0.98f, 0.86f, 0.45f, alpha}, alpha);
        }
        const float dividerY = cursorY + headingH + headingGap * 0.5f;
        UIRenderer::drawRect(boxX, dividerY, p.w, 1.0f, {hue.r, hue.g, hue.b, 0.35f * alpha});
        cursorY += labelBlock;
    }

    const Color body =
        isThought ? Color{hue.r, hue.g, hue.b, 0.92f * alpha} : Color{0.95f, 0.95f, 0.92f, alpha};
    float lineY = cursorY;
    for (const auto& line : revealedLines())
    {
        UIRenderer::drawText(sFont, line, contentX + 2.0f, lineY + 2.0f,
                             {0.0f, 0.0f, 0.0f, 0.5f * alpha});
        UIRenderer::drawText(sFont, line, contentX, lineY, body);
        lineY += bodyLineH;
    }
}

void renderMenu(float ww, float wh, float alpha, float dropOffset)
{
    // Neutral off-white chrome -- a menu is "what you could do here," not a
    // faculty-tinted realization. Same panel/border register as a Line.
    const Color chrome{0.85f, 0.85f, 0.82f, 1.0f};
    const float bodyLineH = static_cast<float>(FontManager::lineHeight(sFont));

    float optW = 0.0f;
    for (const auto& o : sMenuOpts)
        optW = std::max(optW, UIRenderer::measureText(sFont, o.label).width);

    const float contentH = static_cast<float>(sMenuOpts.size()) * bodyLineH;
    const PanelOrigin p = drawPanel(ww, wh, optW, contentH, alpha, dropOffset, 0.88f, chrome);

    // Stash geometry for mouse hit-testing (menuMouse reads these).
    sMenuX = p.x - kPadX;
    sMenuY = p.y;
    sMenuW = p.w;
    sMenuRowH = bodyLineH;

    float lineY = p.y;
    for (std::size_t i = 0; i < sMenuOpts.size(); ++i)
    {
        const bool sel = static_cast<int>(i) == sMenuSel;
        // Selection is shown by color alone (warm gold), no caret prefix.
        // The trailing "Leave" reads dimmer than the deeds -- an exit, not a deed.
        const bool isLeave = sMenuOpts[i].id == kLeaveId;
        const Color c = sel       ? Color{0.98f, 0.90f, 0.55f, alpha} // highlighted: warm gold
                        : isLeave ? Color{0.60f, 0.60f, 0.58f, 0.75f * alpha}
                                  : Color{0.80f, 0.80f, 0.78f, 0.85f * alpha};
        const std::string& text = sMenuOpts[i].label;
        UIRenderer::drawText(sFont, text, p.x + 2.0f, lineY + 2.0f,
                             {0.0f, 0.0f, 0.0f, 0.5f * alpha});
        UIRenderer::drawText(sFont, text, p.x, lineY, c);
        lineY += bodyLineH;
    }
}
} // namespace

void render(const growth::GrowthState& growth, int windowW, int windowH)
{
    if (sItem == ItemKind::None || sFont < 0)
        return;
    const float ww = static_cast<float>(windowW);
    const float wh = static_cast<float>(windowH);
    float alpha = 1.0f;
    float dropOffset = 0.0f;
    entranceAnim(alpha, dropOffset);

    if (sItem == ItemKind::Line)
        renderLine(growth, ww, wh, alpha, dropOffset);
    else if (sItem == ItemKind::Menu)
        renderMenu(ww, wh, alpha, dropOffset);
}
} // namespace thought_box
