#pragma once

// The things a fight is made of. Game-side rather than engine-side: how a body takes damage and
// how effort is spent are this game's rules, and another game on this engine would want its own.

// Health is the ENGINE's (ecs/Components.h) and integer-valued -- a body's hit points are not
// this game's invention, and whole numbers keep damage legible.

// Effort. Spending resets recovery_timer; regen begins only once that has run down, so
// firing continuously never recovers and the decision is when to stop rather than how fast the
// number refills.
struct Stamina
{
    float current = 0.0f;
    float max_stamina = 0.0f;
    float recovery_timer = 0.0f;
};

// The tank on his back. Unlike stamina this does not come back on its own -- see ChargeTuning.
struct Charge
{
    float current = 0.0f;
    float max_charge = 0.0f;
};

// Just been hit. Drives a brief white flash; removed when it runs out.
struct HitFlash
{
    float remaining = 0.0f;
};

// Dying, but not yet gone. Death is a moment rather than an instant: destroying a thing on the
// frame its health runs out means the killing blow is the ONE hit that never flashes, which
// reads as the last hit being the weakest. Held here until the death flash has played.
struct Dying
{
    float remaining = 0.0f;
};

// Time until this creature can hurt the player by touching him again. Per-creature, so a crowd
// does not drain a bar the instant it closes.
struct TouchCooldown
{
    float remaining = 0.0f;
};

// How a creature walks WRONG. Insects do not move on smooth curves: they commit to a slightly
// mistaken heading, hold it a moment, correct, and pause. Held per creature rather than rolled
// per frame -- noise that changes every frame reads as a rendering fault, where an error that
// persists for a fifth of a second reads as a thing making bad decisions.
struct Skitter
{
    float wrong_angle = 0.0f; // radians of heading error currently being committed to
    float until = 0.0f;       // time left holding this error
    float pause_until = 0.0f; // time left standing still
    float phase = 0.0f;       // per-creature offset so a swarm never steps in unison
};

// How far a body has walked, for the code-driven gait. Distance rather than time, so the rock
// belongs to the movement instead of running on its own clock.
struct Gait
{
    float travelled = 0.0f;
    float rest = 0.0f; // 0 walking, 1 fully settled -- eases the hop out instead of freezing it
};

// Marks the vermin. Areas hurt these; the exterminator is not one.
struct Vermin
{
    float contact_damage = 0.0f;
};
