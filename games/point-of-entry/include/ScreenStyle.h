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

// Set the fonts once, after they load.
void init(FontHandle body, FontHandle heading);

// Fill the screen with the overlay.
void dim(int windowW, int windowH);

// Shadow-then-text, the legible-over-anything idiom every surface uses.
void text(const std::string& s, float x, float y, const Color& c);
void textCentered(const std::string& s, float cx, float y, const Color& c);
void headingCentered(const std::string& s, float cx, float y, const Color& c);

// One row of a menu, centred, returning the rect it occupied so the caller can hit-test the
// mouse against exactly what was drawn.
struct Rect
{
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
};
Rect entry(const std::string& label, float cx, float y, bool selected, bool enabled);

// Line height of the body font, for laying a list out.
float lineHeight();

// Is (mx,my) inside r?
bool hit(const Rect& r, float mx, float my);

} // namespace screen_style
