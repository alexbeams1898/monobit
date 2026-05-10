#pragma once

#include "combat/PlayerEquipment.h"

#include <string>

namespace selva::combat
{

// Observer state for the press history. Surfaces "what technique are
// we matching, how many steps in, how rhythmically accurate was the
// last press." Doesn't fire anything; just watches.
struct ChainState
{
    const char* technique_id = nullptr; // matched technique, if any
    int step = 0;                       // 0 = no chain in progress
    float last_press_accuracy = 0.0f;   // 1.0 = window center, 0.0 = edge
    bool last_press_perfect = false;
};

// Per-hand cancel window state. Set when a fire opens the next press's
// rhythm window; read by clipForButton (gates chain advancement) and
// the HUD (renders the green band).
struct CancelWindow
{
    float open_at = 0.0f;
    float close_at = 0.0f;
};

// Record a fired press. Updates internal history + technique match.
// Pass the press button ("LMB"/"RMB"), wall-clock time, the cancel
// window center this fire opened (for next-press accuracy scoring),
// and the cancel window half-width.
void recordPress(const PlayerEquipment& eq, const char* button, float wall_clock_seconds,
                 float cancel_window_center, float cancel_window_half_width);

// Read current state.
const ChainState& chainState();

// Per-hand cancel-window accessors. Setter is called from the fire
// path with the just-opened window times; getter is read by
// clipForButton + the HUD.
void setCancelWindow(HandSide hand, float open_at, float close_at);
const CancelWindow& cancelWindow(HandSide hand);

// Reset history (called on fresh first-strike after a long gap).
void resetChain();

// Tick: clears history if too much time has passed since last press
// (combo_reset_grace_seconds). Pass `one_shot_active=true` to suspend
// the grace timer while a clip is in flight — the player can't input
// the next press during the clip, so the grace shouldn't burn down.
void tickChainObserver(float wall_clock_seconds, bool one_shot_active);

} // namespace selva::combat
