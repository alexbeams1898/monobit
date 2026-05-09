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
struct AttackChainState
{
    int chain_index = 0;
    int technique_index = -1;
    AttackKind chain_kind = AttackKind::Light;
    float cancel_window_open_at = 0.0f;
    float cancel_window_close_at = 0.0f;
    float chain_reset_at = 0.0f;
    float last_press_accuracy = 0.0f;
    bool last_press_was_perfect = false;
    bool is_finisher = false;
};

// Per-hand input buffer: a too-early press during another attack's
// non-cancellable window stays valid for combo_input_buffer_seconds
// and auto-fires when the cancel window opens.
struct BufferedPress
{
    bool pending = false;
    AttackKind kind = AttackKind::Light;
    const char* button = "LMB";
    float buffered_at = 0.0f;
};

// First-press latch out of Peaceful stance. Fires after entry delay
// so locomotion can crossfade from standard_idle into combat-idle
// before the action plays.
enum class PendingFirstActionKind
{
    Attack,
    Block,
};

struct PendingFirstAction
{
    bool active = false;
    PendingFirstActionKind kind = PendingFirstActionKind::Attack;
    HandSide hand = HandSide::Right;
    AttackKind attack_kind = AttackKind::Light;
    const char* button = "LMB";
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

// Reset chain state to "no chain in progress." Called when the chain
// auto-resets after grace period, when attack kind changes mid-chain,
// or when the player explicitly re-engages from an idle.
void resetChain(AttackChainState& chain);

// Per-frame chain-expiry tick. Auto-resets a chain once
// chain_reset_at elapses. Also expires stale buffered presses.
void tickChainExpiry(float wall_clock_seconds, float buffer_seconds);

} // namespace selva::combat
