#pragma once

#include <functional>
#include <string>
#include <vector>

// Shared text-presentation layer for the panel UI (DialogScreen).
// One panel, many producers: dialog conversations, examine boxes,
// future scripted narration popups all push their content into the
// same active-view state. The UI renders whatever's active and
// routes player input back to the active producer via handlers.
//
// Doctrine: per the diamond-foundation rule, the rendering primitive
// is decoupled from the producer-specific backends. DialogSystem
// owns dialog conversation state; Examine owns one-line observations;
// neither knows about the other. Both feed the same panel.

namespace selva::text
{

// Generic shape of "what the panel is currently showing." No
// dialog-specific bookkeeping here -- producers track their own
// state and feed views into this layer for rendering.
struct ActiveTextView
{
    // Optional speaker label shown above the line. Empty = no
    // attribution; the panel shows just the line. Examples:
    //   "The Guide"     (dialog: NPC speaker)
    //   ""              (examine: unattributed narration)
    //   "Grimoire"      (future: scripted Hell-voice text)
    std::string speaker;

    // Body text. Required. The line itself.
    std::string line;

    // Optional choice rows below the line. Empty = the panel shows
    // a "[E] continue" hint and routes E-press to onConfirmAdvance.
    // Non-empty = the panel shows a selectable list with cursor nav,
    // routes choice selection to onSelectChoice(idx).
    struct ChoiceView
    {
        std::string id;
        std::string label;
        bool enabled = true;
        std::string disabled_reason; // shown as tooltip when disabled
    };
    std::vector<ChoiceView> choices;
};

// Producer-supplied callbacks. The text layer calls these in response
// to player input or session end. ALL callbacks are optional; a
// producer can leave any of them empty.
struct SessionHandlers
{
    // Player picked a choice (panel routes the cursor's current idx).
    // Only invoked when the active view has non-empty choices.
    std::function<void(int choice_index)> on_select_choice;

    // Player pressed E/Enter with no choices visible (the
    // press-E-to-continue case). Producer typically calls end() in
    // response, or pushes a new view if it has more to say.
    std::function<void()> on_confirm_advance;

    // Called when end() is invoked OR when another producer's begin()
    // forcibly displaces the active session. Producer should clean
    // up any state it was holding.
    std::function<void()> on_end;
};

// Begin a text-presentation session. Replaces any currently-active
// session (the displaced producer's on_end is invoked first).
//
// `initial` is the first view shown; the producer can update it
// later via updateView(). `handlers` are stored and invoked on
// player input + on session end.
//
// Stamps a "just began this frame" flag that suppresses the FIRST
// frame's input -- the E-press that opened the session should not
// also commit the default choice / dismiss the panel. Cleared at
// the end of the next tick.
void begin(const ActiveTextView& initial, SessionHandlers handlers);

// Update the active view in place. Used by producers (e.g. dialog
// transitioning topics) to swap the displayed content without
// ending the session. No-op if no session is active.
void updateView(const ActiveTextView& v);

// End the active session. Invokes the producer's on_end handler,
// then clears all state. Safe to call when no session is active.
void end();

// True while a session is active. Consumers (input system, UI)
// use this to gate behavior -- e.g. the player input layer
// suppresses gameplay actions while a text panel is up.
bool active();

// Read-only view of the active content. Returns nullptr when no
// session is active. The pointer's lifetime extends until the next
// begin() / updateView() / end() call; copy if held across frames.
const ActiveTextView* currentView();

// Input-layer entry points. UI calls these in response to player
// input. Both are no-ops when no session is active, and both are
// suppressed for one frame after begin() (the just-began guard).
void selectChoice(int choice_index);
void confirmAdvance();

// Per-frame tick. Clears the just-began guard. Called from
// PerFrameTick or main loop after input has been processed for
// this frame.
void tick();

// Clear all runtime state. Called from hardResetWorldForCharacter
// so a panel mid-display doesn't survive a character switch.
void hardReset();

} // namespace selva::text
