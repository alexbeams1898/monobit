#pragma once

#include "combat/PlayerEquipment.h"

namespace selva::combat
{

// Attack-input mapping. Light = bare LMB/RMB. Heavy = Shift+LMB/RMB.
// Running = sprint+LMB/RMB; falls back to Light if the weapon class
// doesn't define a running clip.
enum class AttackKind
{
    Light,
    Heavy,
    Running,
};

// Combo / chain state, per hand. Each hand has its own chain because
// LMB and RMB resolve to different weapons and chains run independently.
// Holds the cancel-window timestamps used by clipForButton to gate chain
// advancement. Press history + matched technique live in ChainObserver.
struct AttackChainState
{
    float cancel_window_open_at = 0.0f;
    float cancel_window_close_at = 0.0f;
    float last_press_accuracy = 0.0f;
    bool last_press_was_perfect = false;
};

// Per-hand input buffer: a too-early press during another attack's
// non-cancellable window stays valid for combo_input_buffer_seconds
// and auto-fires when the cancel window opens.
struct BufferedPress
{
    bool pending = false;
    const char* button = "LMB";
    float buffered_at = 0.0f;
};

// First-press latch out of Peaceful stance for blocks. Fires after entry
// delay so locomotion can crossfade from standard_idle into combat-idle
// before the block raise plays. Attacks no longer use this latch — they
// fire immediately via tryAttackInput.
struct PendingFirstAction
{
    bool active = false;
    const char* block_clip = nullptr;
    bool block_freeze_last = false;
    float fire_at = 0.0f;
};

// Per-hand singletons. Direct access since the per-frame fire path
// reads/writes many fields per call; an accessor wrapper would obscure
// the ownership pattern.
AttackChainState& chain(HandSide hand);
BufferedPress& buffer(HandSide hand);
PendingFirstAction& pendingFirstAction();

// Reset cancel window + accuracy fields to defaults.
void resetChain(AttackChainState& chain);

// Per-frame buffered-press expiry. Buffered presses older than
// `buffer_seconds` are dropped.
void tickChainExpiry(float wall_clock_seconds, float buffer_seconds);

} // namespace selva::combat
