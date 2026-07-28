#pragma once

#include "UIRenderer.h"

#include <string>
#include <vector>

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

// A button at rest: present enough to read as pressable, quiet enough not to
// shout. It lifts to kRowActive under the hand.
inline constexpr Color kButtonRest{0.16f, 0.16f, 0.15f, 0.75f};

// A button that isn't pressable yet: still there, visibly not for you.
inline constexpr Color kButtonOff{0.11f, 0.11f, 0.11f, 0.5f};
inline constexpr float kOffAlpha = 0.3f;

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

// Word-wrap `text` to `max_width` px in `font`. The ONE line-breaking rule --
// two copies would eventually wrap the same sentence differently. A word too
// long to fit alone overflows rather than splitting mid-word (rare for game
// copy).
std::vector<std::string> wrapText(FontHandle font, const std::string& text, float max_width);

// A thing you can press. `hot` = the mouse is over it (or the keyboard cursor is
// on it): it lifts and brightens. `enabled` false draws it dim and inert -- it
// stays where it is rather than vanishing, so a button never moves out from under
// the hand when it becomes pressable. Draws a soft trough + centered label -- the
// register has no hard chrome, so a button is a warm patch, not a box.
//
// Pure draw: hit-testing belongs to the screen (it knows its own layout), which
// keeps one rect from being drawn here and hit-tested slightly differently there.
void button(const std::string& label, float x, float y, float w, float h, bool hot,
            bool enabled = true);

// Whether (mx,my) falls on that same rect. Pairs with button() so the drawn and
// the pressed rect are the same rect.
bool buttonHit(float mx, float my, float x, float y, float w, float h);

// --- the page a thought is written on --------------------------------------
//
// A thought is a notebook entry wherever it appears -- typed out as it lands, or
// read back later in the notebook. It gets one surface, defined here, so those two
// can't drift into two different notebooks: aged paper, ink border, and a
// faculty-hued margin rule down the inside edge (the entry's accent, without
// tinting the whole page).

inline constexpr Color kPaper{0.87f, 0.83f, 0.73f, 1.0f};     // aged parchment backing
inline constexpr Color kInkBorder{0.36f, 0.29f, 0.21f, 1.0f}; // worn ink-brown edge
inline constexpr Color kInkBody{0.17f, 0.14f, 0.11f, 1.0f};   // handwriting ink
inline constexpr Color kInkFaint{0.42f, 0.36f, 0.29f, 1.0f};  // faded ink (datelines, rules)

// A rect's inside, past the padding -- where a panel's content goes.
struct Inset
{
    float x, y, w, h;
};

// Draw the page at (x,y,w,h) and return its content area. `accent` is the faculty
// hue for the margin rule; `alpha` fades the whole panel (the box uses it to drop
// in, screens pass 1).
Inset paperPanel(float x, float y, float w, float h, const Color& accent, float pad_x, float pad_y,
                 float alpha = 1.0f);

// A rectangle's outline, `t` px thick, drawn inside the rect.
void border(float x, float y, float w, float h, const Color& c, float t = 2.0f);

} // namespace screen_style
