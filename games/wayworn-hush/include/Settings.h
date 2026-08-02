#pragma once

#include "HudCanvas.h" // hud::Visibility

// How the player likes the game -- preferences, not progression. These belong to the
// INSTALLATION, not to any one pilgrim: nobody wants to re-answer them per character, so
// they sit beside the roster rather than inside a record (docs/design/SHELL.md).
//
// A plain value with no behavior. The screens read and write it; save/load carries it.
namespace settings
{

// What the HUD shows. `visibility` is the whole-HUD mode; the rest turn individual pieces
// off. A piece toggled off is gone at any visibility -- Off means off.
//
// HUD means STATUS: things on screen because they are always true. The game's speech --
// readings, thoughts, the deed menu, toasts -- is CONTENT and is not settable here; hiding
// it would mute the game, not tidy the screen (see docs/design/HUD.md).
struct Hud
{
    hud::Visibility visibility = hud::Visibility::Auto;
    // The carried watch's readout. Off by default: the walk is meant to be unhurried, and
    // a clock in the corner is a thing to watch. The player who wants it turns it on.
    bool show_time = false;
    bool show_stance = true; // the Observe/Act badge -- it teaches the game's core verb
    // The Spirit total in the corner. On by default: what he has to spend on becoming someone
    // is the one number the game asks the player to carry in their head.
    bool show_spirit = true;
};

struct Settings
{
    Hud hud;
};

// The strings a Visibility is written as, in the save and on screen. One mapping, so the
// file and the UI can't disagree about what "auto" means.
const char* visibilityName(hud::Visibility v);
hud::Visibility visibilityFromName(const char* name, hud::Visibility fallback);

} // namespace settings
