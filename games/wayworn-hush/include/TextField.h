#pragma once

#include <string>

// ---------------------------------------------------------------------------
// TextField -- a line of text the player types, in the game's own register.
//
// The studio has never had one: prison-escape hand-rolls the same ~35 lines
// inside its name screen (no key-repeat, no cursor movement), and selva dodges
// it with an ImGui box that looks nothing like the game. This is the honest
// version -- the state and the rules here, the drawing left to the screen that
// owns it, so it can look like whatever it sits in.
//
// Deliberately small: a name is a line, not a document. No selection, no
// clipboard, no multi-line. Cursor movement and key-repeat ARE included because
// their absence is what makes a field feel broken.
// ---------------------------------------------------------------------------

namespace text_field
{

// How many characters a field will hold. A name is short by nature; the cap
// exists so a field can't outgrow the space drawn for it.
inline constexpr int kMaxLength = 20;

struct State
{
    std::string text;
    int cursor = 0; // insertion point, in [0, text.size()]

    // Key-repeat: how long the current held edit key has been down, and when it
    // last fired. A field without repeat forces one keypress per character
    // deleted, which reads as broken.
    double held_secs = 0.0;
    double last_fire_secs = 0.0;
};

// The keys a field acts on this frame, decoded by the caller so this module
// stays free of SDL types. `typed` is the frame's text input (already filtered
// of control characters by the platform).
struct Input
{
    std::string typed;
    bool backspace = false; // held, not edge-triggered -- repeat needs the hold
    bool del = false;
    bool left = false;
    bool right = false;
    bool home = false;
    bool end = false;
};

// Start a field holding `initial`, cursor at the end.
void begin(State& state, const std::string& initial = {});

// Advance one frame: insert what was typed, apply edits (with key-repeat driven
// by `dt`), move the cursor. Everything is clamped -- a cursor can't leave the
// text and the text can't pass kMaxLength.
void update(State& state, const Input& input, double dt);

// Whether `text` is a usable name: something other than whitespace. Deliberately
// permissive -- duplicates are fine (a pilgrim is their id, not their name), any
// printable text is fine. Only "nothing at all" is refused.
bool acceptable(const std::string& text);

} // namespace text_field
