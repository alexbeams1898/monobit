#pragma once

#include "UIRenderer.h"

#include <string>

using FontHandle = int;

// ---------------------------------------------------------------------------
// ScreenStyle -- the game's one on-screen register, shared by every full-screen
// surface (the title, the pause page). Wayworn's look is minimal: muted
// shadow-text over a soft darkening of whatever is behind, no ornate chrome.
//
// Here so the screens can't drift into two slightly-different palettes -- one
// definition of "what this game looks like". Feel/placement stays with each
// screen; only the vocabulary is shared. See docs/design/AESTHETIC.md.
// ---------------------------------------------------------------------------

namespace screen_style
{

// The darkening laid over the frozen world. Near-solid: text is what matters,
// the world behind is a held breath, not a backdrop competing for the eye.
inline constexpr Color kOverlay{0.04f, 0.05f, 0.06f, 0.92f};

inline constexpr Color kText{0.95f, 0.95f, 0.92f, 1.0f};
inline constexpr Color kTextDim{0.95f, 0.95f, 0.92f, 0.45f};
inline constexpr Color kShadow{0.0f, 0.0f, 0.0f, 0.55f};

// A selected/hovered row's warm lift.
inline constexpr Color kRowActive{0.30f, 0.29f, 0.24f, 0.85f};

// The alpha an unselected item reads at -- brightness alone marks a selection
// (no boxes, no carets).
inline constexpr float kIdleAlpha = 0.55f;

// Set the fonts once, after FontManager loads them.
void init(FontHandle body, FontHandle label);

FontHandle bodyFont();
FontHandle labelFont();

// Draw shadow-then-text: the whole game's legible-over-world idiom. `alpha`
// scales both so callers can dim inactive items.
void softText(const std::string& s, float x, float y, const Color& c, float alpha = 1.0f);

// Centered on `cx`.
void softTextCentered(const std::string& s, float cx, float y, const Color& c, float alpha = 1.0f);

// A line's full height for the given font (its line height plus the register's
// breathing room). The unit vertical rhythm every screen lays out on.
float lineH(FontHandle font);

} // namespace screen_style
