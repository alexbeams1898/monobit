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

// PAGE ANATOMY. Every full-screen surface shares one skeleton -- heading at the same height,
// content starting the same distance under it, rows on the same rhythm -- so screens are the
// same page wearing different words. A screen inventing its own proportions is drift.
float pageHeadingY(int windowH); // where the heading sits
float pageContentY(int windowH); // where the content begins
float pageRowH();                // the list rhythm

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

// One row of a menu, centred, returning the rect it occupied so the caller can hit-test the
// mouse against exactly what was drawn.
Rect entry(const std::string& label, float cx, float y, bool selected, bool enabled);

// Line height of the body font, for laying a list out.
float lineHeight();

// Is (mx,my) inside r?
bool hit(const Rect& r, float mx, float my);

} // namespace screen_style
