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

BufferedPress& buffer(HandSide hand);
PendingFirstAction& pendingFirstAction();

// Per-frame buffered-press expiry. Buffered presses older than
// `buffer_seconds` are dropped.
void tickChainExpiry(float wall_clock_seconds, float buffer_seconds);

} // namespace selva::combat
