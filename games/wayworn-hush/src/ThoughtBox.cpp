#include "ThoughtBox.h"

#include "FontManager.h"
#include "HudCanvas.h"
#include "Notify.h"
#include "ReadingColor.h"
#include "ScreenStyle.h"
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
hud::Regions sRegions; // fixed HUD bands (canvas fractions); content renders into them

// Gap under the header divider (faculty . rarity . New), between it and the body.
// Shared by loadLine (page-capacity math) and renderLine (layout) so they agree.
constexpr float kHeadingGap = 12.0f;

// The notebook palette is the game's, not this box's -- the pause page reads the same
// entries back on the same page (see screen_style).
constexpr Color kInkBody = screen_style::kInkBody;
constexpr Color kInkFaint = screen_style::kInkFaint;

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
psyche::PendingLine sLine; // reading on screen (empty text = none)
Phase sPhase = Phase::None;
float sPhaseT = 0.0f;              // seconds elapsed in the current phase
int sRevealed = 0;                 // characters of the CURRENT PAGE revealed so far
int sLastBlip = 0;                 // revealed-count at the last blip (rate-limits the tick)
std::vector<std::string> sWrapped; // body wrapped into lines (built when a line loads)
// Pagination: a Line that overflows its fixed region is shown one page (a run of
// wrapped lines that fit the region height) at a time. Space reveals the page,
// then turns to the next; the last page's Space dismisses. sPageStart is the first
// wrapped-line index of the page on screen; sLinesPerPage is set when the line loads
// (region height is fixed once window dims are known).
std::size_t sPageStart = 0;
int sLinesPerPage = 0; // 0 until a line loads; >=1 thereafter

// --- Menu state ---
// The deed menu: the spot's offered actions, shown after its observation reading is read
// (the EarthBound "Check it, then choose what to do"). W/S select, Space/click confirm,
// F/RMB closes.
struct Option
{
    std::string id;    // action id (for takeAction)
    std::string label; // shown text
};
std::string sMenuSpot;         // observable id the menu belongs to
std::vector<Option> sMenuOpts; // current offered deeds
int sMenuSel = 0;              // highlighted option
bool sMenuQueued = false;      // the deed menu wants to re-open once state.pending is empty
                               // (after a deed's result line has been read)
bool sMenuMustChoose = false;  // a decision that cannot be walked away from: no Leave
                               // option, F/RMB ignored -- only a consuming deed ends it
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

// The wrapped-line range [begin, end) of the page currently on screen.
std::size_t pageEnd()
{
    return std::min(sWrapped.size(),
                    sPageStart + static_cast<std::size_t>(std::max(1, sLinesPerPage)));
}

// Characters in the current page (typewriter reveals within the page, not the
// whole body -- each page types out fresh).
int totalChars()
{
    int n = 0;
    for (std::size_t i = sPageStart; i < pageEnd(); ++i)
        n += static_cast<int>(sWrapped[i].size());
    return n;
}

// True while pages remain after the one on screen (Space turns rather than dismisses).
bool hasMorePages()
{
    return pageEnd() < sWrapped.size();
}

// Advance to the next page: restart the typewriter for its lines.
void turnPage()
{
    sPageStart = pageEnd();
    sRevealed = 0;
    sLastBlip = 0;
    sPhase = Phase::Typing;
    sPhaseT = 0.0f;
}

// Play the drop-in sound for the line now in sLine. A NEW thought (a fresh notebook
// entry) gets the pen-on-paper writing sound; everything else gets the generic
// appear tone, pitch/volume scaled by rarity + a shimmer layer for rare+ readings.
void playAppearSfx()
{
    if (sLine.kind == psyche::LineKind::Thought && sLine.is_new)
    {
        AudioSystem::playSfx(sCfg.notebook_sound, 0.8f, 1.0f);
        return;
    }
    const float tier = static_cast<float>(std::max(0, sLine.difficulty - 1)); // 0..4
    float pitch = 1.0f + tier * 0.06f;
    float vol = 0.7f * (1.0f + tier * 0.12f);
    // An impression arrives on the senses, not by choice -- it announces itself
    // more softly and a shade lower than a chosen looking.
    if (sLine.kind == psyche::LineKind::Impression)
    {
        vol *= 0.7f;
        pitch *= 0.92f;
    }
    AudioSystem::playSfx(sCfg.appear_sound, vol, pitch);
    if (sLine.difficulty >= 4) // Rare / Legendary get a brighter shimmer layer
        AudioSystem::playSfx(sCfg.appear_sound, vol * 0.5f, pitch * 1.6f);
}

void loadLine(psyche::PendingLine line, int windowW, int windowH)
{
    sItem = ItemKind::Line;
    sLine = std::move(line);
    // Speech wears quotes -- someone else's words in the perception window. The
    // speaker's NAME renders as the line's header (see renderLine).
    if (!sLine.speaker.empty())
        sLine.text = "\"" + sLine.text + "\"";
    sPhase = Phase::DropIn;
    sPhaseT = 0.0f;
    sRevealed = 0;
    sLastBlip = 0;
    sPageStart = 0;
    // Wrap + paginate to the content band's inner width -- ALL boxes render
    // there, distinguished by register (paper vs window), never by position.
    const hud::Rect region = hud::resolve(sRegions.content, windowW, windowH);
    const float padX = sRegions.pad_x * hud::scale(windowW, windowH);
    const float padY = sRegions.pad_y * hud::scale(windowW, windowH);
    sWrapped = wrapText(sLine.text, region.w - 2.0f * padX);
    // Page capacity: how many body lines fit the region's inner height, reserving
    // the header block so a thought's header-bearing first page fits without
    // clipping (harmless slight over-reserve for a headerless observation).
    const float bodyLineH = static_cast<float>(FontManager::lineHeight(sFont));
    const float headingH = static_cast<float>(FontManager::lineHeight(sHeadingFont));
    const float innerH = region.h - 2.0f * padY - (headingH + kHeadingGap);
    sLinesPerPage = std::max(1, static_cast<int>(innerH / bodyLineH));
    playAppearSfx();
    // The reward toast lands WITH the line on screen (not when the engine queued
    // it seconds earlier), so "+N Spirit" reads alongside the thought that earned it.
    if (sLine.spirit_exp > 0)
        notify::push("+" + std::to_string(sLine.spirit_exp) + " Spirit",
                     {0.98f, 0.90f, 0.55f, 1.0f});
}

// Sentinel option id: always the last entry, closes the menu (equivalent to F).
const char* const kLeaveId = "__leave__";

// Fill the DEED menu with the spot's currently-offered deeds plus a trailing "Leave".
// Returns false when there are no real deeds AND `alwaysShow` is false -- so the reading's
// AUTO-follow menu doesn't open a Leave-only box, but the RUNNING/Act path (alwaysShow) does
// (a spot with nothing to do still shows "Leave").
bool buildMenu(const psyche::State& state, const growth::GrowthState& growth,
               const std::string& spot, bool alwaysShow = false)
{
    sMenuOpts.clear();
    for (const auto* a : psyche::availableActions(state, growth, spot))
        sMenuOpts.push_back(Option{a->id, a->label});
    if (sMenuOpts.empty() && !alwaysShow)
        return false;
    // A must-choose menu offers no exit -- unless it has run out of deeds, which is
    // an authoring error (a decision with nothing to decide); Leave then keeps the
    // game playable instead of trapping the player in an empty box.
    if (sMenuMustChoose && sMenuOpts.empty())
        std::fprintf(stderr, "[box] must-choose menu for '%s' has no deeds\n", spot.c_str());
    if (!sMenuMustChoose || sMenuOpts.empty())
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

// The padded content area of a region, in window pixels. Two registers:
//   notebook=true  -> a THOUGHT: the notebook page (screen_style::paperPanel -- the same
//                     surface the pause page reads these entries back on, so a thought is
//                     one thing whether it's landing or being looked up).
//   notebook=false -> an OBSERVATION / menu: a dark dialogue window with a faculty-neutral
//                     (or lightly accented) frame -- he is perceiving the world, not
//                     writing it down.
// Content renders within {x,y,w,h}; the region is a FIXED band (no content-sizing).
using ContentArea = screen_style::Inset;

ContentArea drawPanel(const hud::Rect& region, int windowW, int windowH, float alpha,
                      float dropOffset, bool notebook, const Color& accent)
{
    const float padX = sRegions.pad_x * hud::scale(windowW, windowH);
    const float padY = sRegions.pad_y * hud::scale(windowW, windowH);
    const float x = region.x;
    const float y = region.y + dropOffset;

    if (notebook)
        return screen_style::paperPanel(x, y, region.w, region.h, accent, padX, padY, alpha);

    // Dark dialogue window: cool backing + a soft accent-tinted frame.
    UIRenderer::drawRect(x, y, region.w, region.h, {0.05f, 0.06f, 0.07f, 0.85f * alpha});
    screen_style::border(x, y, region.w, region.h, {accent.r, accent.g, accent.b, 0.55f * alpha});
    return {x + padX, y + padY, region.w - padX * 2.0f, region.h - padY * 2.0f};
}

// The current page's lines, truncated to the typewriter reveal so far.
std::vector<std::string> revealedLines()
{
    std::vector<std::string> out;
    int budget = sRevealed;
    for (std::size_t i = sPageStart; i < pageEnd(); ++i)
    {
        if (budget <= 0)
            break;
        const std::string& full = sWrapped[i];
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

void init(FontHandle body_font, FontHandle heading_font, const Config& config,
          const hud::Regions& regions)
{
    sFont = body_font;
    sHeadingFont = heading_font;
    sCfg = config;
    sRegions = regions;
}

void update(psyche::State& state, const growth::GrowthState& growth, float dt, int windowW,
            int windowH)
{
    if (sItem == ItemKind::None)
    {
        // Reading / result lines ALWAYS drain first -- the menu waits until the
        // player has read everything queued (so a spot's readings show before its
        // deed menu, and a deed's result_text shows before the menu re-opens).
        if (!state.pending.empty())
        {
            psyche::PendingLine next = state.pending.front();
            state.pending.pop_front();
            loadLine(std::move(next), windowW, windowH);
            return;
        }
        // Pending is empty: re-open the deed menu after a deed's result line was read (Act
        // stance). Always shows -- a spot whose last deed was taken still offers "Leave".
        if (sMenuQueued)
        {
            buildMenu(state, growth, sMenuSpot, /*alwaysShow=*/true);
            openMenu();
            return;
        }
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

bool activeLineEarnedSpirit()
{
    return sItem == ItemKind::Line && sPhase != Phase::FadeOut && sLine.spirit_exp > 0;
}

bool activeLineKind(psyche::LineKind& out)
{
    if (sItem != ItemKind::Line || sPhase == Phase::FadeOut)
        return false;
    out = sLine.kind;
    return true;
}

bool fullyShown()
{
    return sItem != ItemKind::None && sPhase == Phase::Done;
}

bool settled()
{
    // Fully at rest: nothing on screen AND no menu re-open queued. Distinct from
    // !active(): the box's own lifecycle has one-frame gaps (a result line fading
    // out before the queued deed menu re-opens) where nothing is showing but the
    // exchange is NOT over -- a waiter that read active() would slip through them.
    return sItem == ItemKind::None && !sMenuQueued;
}

bool activeThought(const growth::GrowthState& growth, Color& out_color)
{
    if (sItem != ItemKind::Line || sLine.kind != psyche::LineKind::Thought ||
        sPhase == Phase::FadeOut)
        return false;
    out_color = reading_color::forReading(growth, sLine.faculty, sLine.difficulty, 1.0f);
    return true;
}

psyche::ObserveResult pushObserve(psyche::State& state, const growth::GrowthState& growth,
                                  const std::string& spot, const psyche::RollRng& rng,
                                  bool impression)
{
    // Run the spot's reading (the WALKING/Observe stance: queues lines; update() drains them).
    // The whole result goes back to the caller -- the EXP to bank, and any thoughts that
    // landed, which the game writes into the notebook. The box adds nothing to it.
    sMenuSpot = spot;
    return psyche::observeById(state, growth, spot, rng, impression);
}

void openDeedMenu(psyche::State& state, const growth::GrowthState& growth, const std::string& spot,
                  bool must_choose)
{
    // The RUNNING/Act stance: open the deed menu immediately. Always shows, even if the spot
    // has no deeds right now -- you get a "Leave"-only menu, not a dead press.
    sMenuSpot = spot;
    sMenuMustChoose = must_choose;
    buildMenu(state, growth, spot, /*alwaysShow=*/true);
    openMenu();
}

ConfirmResult confirm(psyche::State& state, const growth::GrowthState& growth,
                      const psyche::RollRng& rng)
{
    if (sItem == ItemKind::Line)
    {
        if (sPhase == Phase::Typing)
        {
            sRevealed = totalChars(); // reveal the rest of this page
            sPhase = Phase::Done;
            sPhaseT = 0.0f;
        }
        else if (sPhase == Phase::Done)
        {
            if (hasMorePages())
                turnPage(); // more to read -> next page, not dismiss
            else
            {
                sPhase = Phase::FadeOut; // last page -> dismiss
                sPhaseT = 0.0f;
            }
        }
        return {};
    }
    if (sItem == ItemKind::Menu && sPhase == Phase::Done)
    {
        if (sMenuSel < 0 || sMenuSel >= static_cast<int>(sMenuOpts.size()))
            return {};
        const std::string actionId = sMenuOpts[static_cast<std::size_t>(sMenuSel)].id;
        if (actionId == kLeaveId)
        {
            back(); // the trailing "Leave" option closes the menu
            return {};
        }
        // Take the chosen deed. Its result_text enqueues as a Line; after it's read, update()
        // re-opens the deed menu with the remaining deeds. Hand back the EXP + any grants so
        // the caller banks + deposits them.
        sItem = ItemKind::None;
        sPhase = Phase::None;
        const psyche::ObserveResult r = psyche::takeAction(state, growth, sMenuSpot, actionId, rng);
        // A deed that CONSUMES the spot leaves nothing to return to -- don't re-queue.
        // That is also how a decision menu ends: its terminal deeds consume.
        sMenuQueued = r.consumed_spot.empty();
        if (!r.consumed_spot.empty())
            sMenuMustChoose = false; // decided -- the box may close normally
        return {r.earned,     r.granted,       r.gathered,     r.taught, r.landed,
                r.stat_gains, r.consumed_spot, /*acted=*/true, r.minutes};
    }
    return {};
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

ConfirmResult menuMouse(psyche::State& state, const growth::GrowthState& growth,
                        const psyche::RollRng& rng, float mx, float my, bool clicked)
{
    if (!menuActive() || sPhase != Phase::Done || sMenuOpts.empty())
        return {};
    // Which option row is the cursor over? (Rows are stacked from sMenuY.)
    if (mx < sMenuX || mx > sMenuX + sMenuW)
        return {};
    const int row = static_cast<int>((my - sMenuY) / sMenuRowH);
    if (row < 0 || row >= static_cast<int>(sMenuOpts.size()))
        return {};
    sMenuSel = row;                                                 // hover highlights
    return clicked ? confirm(state, growth, rng) : ConfirmResult{}; // click confirms
}

void back()
{
    if (sItem == ItemKind::Menu && !sMenuMustChoose)
    {
        sMenuQueued = false; // don't re-open; leaving the spot
        sPhase = Phase::FadeOut;
        sPhaseT = 0.0f;
    }
}

void reset()
{
    // Everything the box carries between frames. Unlike back(), this doesn't fade out --
    // the world it belonged to is gone, so there is nothing to fade from.
    sItem = ItemKind::None;
    sPhase = Phase::None;
    sPhaseT = 0.0f;
    sRevealed = 0;
    sLastBlip = 0;
    sWrapped.clear();
    sPageStart = 0;
    sLinesPerPage = 0;
    sMenuSpot.clear();
    sMenuOpts.clear();
    sMenuSel = 0;
    sMenuQueued = false;
    sMenuMustChoose = false;
    sMenuX = 0.0f;
    sMenuY = 0.0f;
    sMenuW = 0.0f;
    sMenuRowH = 0.0f;
}

// --- render ----------------------------------------------------------------
namespace
{
void renderLine(const growth::GrowthState& growth, int windowW, int windowH, float alpha,
                float dropOffset)
{
    const Color hue = reading_color::forReading(growth, sLine.faculty, sLine.difficulty, 1.0f);
    // Both kinds render in the single LOWER band, distinguished by REGISTER, not
    // position: a THOUGHT is a notebook entry (paper + ink, dated, headered with its
    // faculty/rarity identity); an OBSERVATION is just perceived -- a bare line in a
    // dark window, no header (the register itself signals the kind).
    const bool isThought = sLine.kind == psyche::LineKind::Thought;
    // Speech: someone else's line in the perception window -- headed by the
    // speaker's name instead of a faculty/rarity identity.
    const bool isSpeech = !isThought && !sLine.speaker.empty();
    const bool showHeader = (isThought || isSpeech) && sPageStart == 0;
    const hud::Rect region = hud::resolve(sRegions.content, windowW, windowH);

    const ContentArea c = drawPanel(region, windowW, windowH, alpha, dropOffset, isThought, hue);

    // Ink on paper for a notebook thought; light text in the perception window.
    const Color faint = isThought ? kInkFaint : Color{0.72f, 0.74f, 0.72f, 1.0f};
    const Color body = isThought ? Color{kInkBody.r, kInkBody.g, kInkBody.b, alpha}
                                 : Color{0.95f, 0.95f, 0.92f, alpha};

    const float headingH = static_cast<float>(FontManager::lineHeight(sHeadingFont));
    const float bodyLineH = static_cast<float>(FontManager::lineHeight(sFont));

    float cursorY = c.y;
    if (showHeader)
    {
        if (isSpeech)
        {
            // The speaker's name, plain and light -- who is talking, nothing more.
            UIRenderer::drawText(sHeadingFont, sLine.speaker, c.x, cursorY,
                                 {0.85f, 0.85f, 0.82f, alpha});
        }
        else
        {
            // Faculty (the stat) pinned LEFT, in its hue -- the thought's identity.
            UIRenderer::drawText(sHeadingFont, reading_color::facultyLabel(sLine.faculty), c.x,
                                 cursorY, {hue.r, hue.g, hue.b, alpha});

            // Rarity emphasized TOP-RIGHT as a badge: a DARK solid pill outlined in the
            // rarity color, with LIGHT text -- reads crisp, not faded.
            const std::string rarity = reading_color::rarityWord(sLine.difficulty);
            const Color rc = reading_color::rarityColor(sLine.difficulty, 1.0f);
            const float rw = UIRenderer::measureText(sHeadingFont, rarity).width;
            const float padH = 10.0f;
            const float padV = 4.0f;
            const float pillX = c.x + c.w - rw - padH * 2.0f;
            UIRenderer::drawRect(pillX, cursorY - padV, rw + padH * 2.0f, headingH + padV,
                                 {0.10f, 0.09f, 0.08f, 0.92f * alpha});
            screen_style::border(pillX, cursorY - padV, rw + padH * 2.0f, headingH + padV,
                                 {rc.r, rc.g, rc.b, alpha});
            UIRenderer::drawText(sHeadingFont, rarity, pillX + padH, cursorY,
                                 {0.97f, 0.96f, 0.93f, alpha});
        }

        const float dividerY = cursorY + headingH + kHeadingGap * 0.5f;
        UIRenderer::drawRect(c.x, dividerY, c.w, 1.0f, {faint.r, faint.g, faint.b, 0.45f * alpha});
        cursorY += headingH + kHeadingGap;
    }

    for (const auto& line : revealedLines())
    {
        UIRenderer::drawText(sFont, line, c.x, cursorY, body);
        cursorY += bodyLineH;
    }

    // A "more pages" cue, bottom-right of the region once the page finishes typing
    // (Space then turns the page rather than dismissing).
    if (sPhase == Phase::Done && hasMorePages())
    {
        const float tagW = UIRenderer::measureText(sHeadingFont, "v").width;
        UIRenderer::drawText(sHeadingFont, "v", c.x + c.w - tagW, c.y + c.h - headingH,
                             {faint.r, faint.g, faint.b, alpha});
    }
}

void renderMenu(int windowW, int windowH, float alpha, float dropOffset)
{
    // A menu is a deed on the object you perceive, not a notebook entry -- so it's
    // the OBSERVATION register (dark dialogue window), selection in warm gold.
    const Color chrome{0.85f, 0.85f, 0.82f, 1.0f};
    const float bodyLineH = static_cast<float>(FontManager::lineHeight(sFont));
    const hud::Rect region = hud::resolve(sRegions.content, windowW, windowH);
    const ContentArea c =
        drawPanel(region, windowW, windowH, alpha, dropOffset, /*notebook=*/false, chrome);

    // Stash geometry for mouse hit-testing (menuMouse reads these).
    sMenuX = c.x;
    sMenuY = c.y;
    sMenuW = c.w;
    sMenuRowH = bodyLineH;

    float lineY = c.y;
    for (std::size_t i = 0; i < sMenuOpts.size(); ++i)
    {
        const bool sel = static_cast<int>(i) == sMenuSel;
        // Selection is warm gold; the trailing "Leave" reads dimmer than the deeds.
        const bool isLeave = sMenuOpts[i].id == kLeaveId;
        const Color col = sel       ? Color{0.98f, 0.90f, 0.55f, alpha}
                          : isLeave ? Color{0.60f, 0.60f, 0.58f, 0.75f * alpha}
                                    : Color{0.85f, 0.85f, 0.82f, alpha};
        const std::string& text = sMenuOpts[i].label;
        UIRenderer::drawText(sFont, text, c.x, lineY, col);
        lineY += bodyLineH;
    }
}
} // namespace

void render(const growth::GrowthState& growth, int windowW, int windowH)
{
    if (sItem == ItemKind::None || sFont < 0)
        return;
    float alpha = 1.0f;
    float dropOffset = 0.0f;
    entranceAnim(alpha, dropOffset);

    if (sItem == ItemKind::Line)
        renderLine(growth, windowW, windowH, alpha, dropOffset);
    else if (sItem == ItemKind::Menu)
        renderMenu(windowW, windowH, alpha, dropOffset);
}
} // namespace thought_box
