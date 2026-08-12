#pragma once

#include "UIRenderer.h"

#include <string>

using FontHandle = int;

// The game's one on-screen register, shared by every full-screen surface (the title, the
// pause screen). Here so those surfaces cannot drift into two slightly-different palettes --
// one definition of what this game looks like.
//
// The look: a bare list in a dim room. Nothing ornate, nothing chromed. This is a game about
// a man doing a job, and its menus should read like a clipboard rather than a title card.
namespace screen_style
{

// The darkening laid over whatever is behind. Near-solid: the words are what matter, and the
// world behind them is a held breath rather than a backdrop competing for the eye.
inline constexpr Color kOverlay{0.03f, 0.03f, 0.04f, 0.94f};

inline constexpr Color kText{0.90f, 0.89f, 0.84f, 1.0f};
inline constexpr Color kTextDim{0.90f, 0.89f, 0.84f, 0.40f}; // an entry not for you
inline constexpr Color kTextHot{1.00f, 0.97f, 0.88f, 1.0f};  // the one under the cursor
inline constexpr Color kShadow{0.0f, 0.0f, 0.0f, 0.6f};
inline constexpr Color kAccent{0.72f, 0.22f, 0.18f, 1.0f}; // the red of a point of entry

// THE REST OF THE PALETTE. Red is danger, amber is spend-or-decide, green is earn-or-recover
// -- a colour outside this list is a colour the game does not have (enforced:
// scripts/check_design.py).
inline constexpr Color kBarBack{0.0f, 0.0f, 0.0f, 0.7f};
inline constexpr Color kHealth{0.75f, 0.2f, 0.18f, 0.95f};
inline constexpr Color kStaminaSpent{0.85f, 0.6f, 0.25f, 0.95f};
inline constexpr Color kStaminaFresh{0.45f, 0.8f, 0.35f, 0.95f};
inline constexpr Color kGain{0.55f, 0.85f, 0.4f, 1.0f};
inline constexpr Color kReticle{0.95f, 0.95f, 0.9f, 0.9f};
inline constexpr Color kPanel{0.05f, 0.05f, 0.06f, 0.85f};
inline constexpr Color kPanelEdge{0.75f, 0.72f, 0.62f, 0.5f};
inline constexpr Color kDamage{0.95f, 0.85f, 0.4f, 1.0f}; // a hit's number
inline constexpr Color kCallout{1.0f, 1.0f, 1.0f, 1.0f};  // a kill's number

// THE PAPER. A manila sheet for surfaces that read as a DOCUMENT rather than a menu -- the
// trade's paperwork, in the family of kPanelEdge's khaki. Ink is its text and its border;
// the accent doubles as the sheet's margin rule.
inline constexpr Color kPaper{0.78f, 0.72f, 0.57f, 1.0f};
inline constexpr Color kInk{0.18f, 0.15f, 0.11f, 1.0f};
inline constexpr Color kInkDim{0.18f, 0.15f, 0.11f, 0.62f}; // a section's body, under its head

// A palette colour at a different strength -- the ONE sanctioned way to vary alpha, so fades
// never mint new colours.
Color withAlpha(const Color& c, float a);
// Black at a given strength, for full-screen fades.
Color black(float a);

// Load the game's one face at both sizes and register them. THE INTEGER LADDER: sizes are
// base-pixels times a whole-number scale from the window height, because a fractional size
// blurs a pixel face -- the UI obeys the same integer law as the world.
void initFonts(int windowH);

// THE SPACING UNIT: 4 base pixels times the same ladder scale as the fonts, so margins and
// padding grow WITH the type instead of drifting away from it. Every shell distance is a
// whole number of units -- pad(2) between related things, pad(4) as the screen margin --
// and a distance that is not units is a distance the layout does not have.
int uiScale();        // the ladder's k
float pad(int units); // units * 4 * k

// The rect the idioms lay text into. Named before them because C++ binds names at first
// sight -- declared after, the engine's GLOBAL Rect gets silently bound instead.
struct Rect
{
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
};

// Alignment is an IDIOM, not per-site math (enforced: check_design.py forbids measureText
// and drawText outside this file). textRight anchors the string's right edge; textInBox
// truly centres vertically from the font's real metrics -- no guessed half-heights.
void textRight(const std::string& s, float xRight, float y, const Color& c);
void textInBox(const std::string& s, const Rect& box, const Color& c, bool alignRight = false);

// A bordered panel -- the shell's one box: edge frame, inset field. The pocket, the weapon
// block, and whatever needs a box next all draw this, so boxes cannot drift into siblings.
void panel(const Rect& r);

// A paper sheet: ink frame, manila field, the accent as its margin rule. Its text goes
// through the ink idioms below.
void paperPanel(const Rect& r);

// PAGE ANATOMY. Every full-screen surface shares one skeleton -- heading at the same height,
// content starting the same distance under it, rows on the same rhythm -- so screens are the
// same page wearing different words. A screen inventing its own proportions is drift.
//
// THE FRAME'S RULE: the page is inset from the window by one margin spent on every side, and
// padded inside by one more; the page's HEAD sits at the top of the padded field and everything
// else follows it down the row rhythm. So the page is centred, its field is evenly inset, and
// where the head lands is a consequence of the frame rather than a number chosen twice.
//
// The head is whatever names the page: a heading where there is one thing to name, a tab strip
// where the page has parts. A screen does not wear both -- a title over a strip of tabs that
// already say where you are is a word spent on nothing.
float pageHeadingY(int windowH); // where the heading sits
float pageContentY(int windowH); // where the content begins
float pageRowH();                // the list rhythm

// THE COLUMN -- the anatomy's horizontal half. The backing panel, the tab strip, every list
// row and the paper card are all cut from this one measure, so a page's frame can never
// disagree with what it frames and two screens cannot lay their rows out to different widths.
// In spacing units, which ride the same ladder as the type.
float pageColumnW();
float pageColumnLeft(float cx);
// One list row at y -- drawn into and hit-tested against the same rect, so a hover can never
// cover a different area than the row it lights up.
Rect pageRow(float cx, float y);
// One cell of a strip of `count` tabs laid across the column.
Rect pageTab(float cx, float y, int index, int count);
// A tab stop inside a row -- t running 0..1 across the column, for form rows carrying more
// than a left label and a right value.
float pageStop(const Rect& row, float t);
// A LINK: anything that can be taken -- a menu row, a guide entry, a tab. How this game shows
// which one is under the cursor happens to the WORDS, not to a bar behind them: the label
// brightens and takes a bracket either side. Nothing has to be sized or aligned to a shape,
// and a label of any length carries its own marks.
enum class LinkState
{
    Faint, // there, but not where you are
    Idle,  // available
    Hot,   // the one that will be taken
};
void link(const std::string& s, float x, float y, LinkState state);
void linkCentered(const std::string& s, float cx, float y, LinkState state);
// The page's backing: the column plus its margin.
Rect pagePanelRect(int windowW, int windowH);
// Where a page's field ends -- the last line content may occupy.
float pageBottom(int windowH);
// A band of the column between two heights -- the whole width of the page's field.
Rect pageBand(int windowW, float top, float bottom);
// A page divided down the middle: a pane and, beside it, the list that drives it. `share` is
// how much of the column the left side takes; the gutter comes out of the middle, so the two
// sides can never overlap or drift apart.
struct Split
{
    Rect left, right;
};
Split pageSplit(int windowW, float top, float bottom, float share);
// One list row inside a band -- drawn into and hit-tested against the same rect. pageRow is
// this for a list that owns the whole column.
Rect bandRow(const Rect& band, float y);

// THE TITLE PAGE deviates on purpose, the genre's shape: the name large in the upper third,
// the menu low. Named here so the deviation is a standard, not a screen's opinion.
float titleY(int windowH);
float titleMenuY(int windowH);
// The display size -- the title's own, 48 base px on the ladder.
void displayCentered(const std::string& s, float cx, float y, const Color& c);

// Fill the screen with the overlay.
void dim(int windowW, int windowH);

// Shadow-then-text, the legible-over-anything idiom every surface uses.
int bodyFont();

void text(const std::string& s, float x, float y, const Color& c);
void textCentered(const std::string& s, float cx, float y, const Color& c);
void headingCentered(const std::string& s, float cx, float y, const Color& c);

// Text on paper. Ink on a light field carries its own contrast, and a shadow there reads as
// misregistration rather than depth -- the glyphs alone, no shadow pass.
void inkText(const std::string& s, float x, float y, const Color& c);
void inkTextCentered(const std::string& s, float cx, float y, const Color& c);

// One row of a menu, centred, returning the rect it occupied so the caller can hit-test the
// mouse against exactly what was drawn.
Rect entry(const std::string& label, float cx, float y, bool selected, bool enabled);

// Line height of the body font, for laying a list out.
float lineHeight();

// Is (mx,my) inside r?
bool hit(const Rect& r, float mx, float my);

} // namespace screen_style
