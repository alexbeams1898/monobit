#pragma once

// The things a fight is made of. Game-side rather than engine-side: how a body takes damage and
// how effort is spent are this game's rules, and another game on this engine would want its own.

// Health is the ENGINE's (ecs/Components.h) and integer-valued -- a body's hit points are not
// this game's invention, and whole numbers keep damage legible.

// The souls bar. Spending resets recovery_timer; regen begins only once that has run down, so
// firing continuously never recovers and the decision is when to stop rather than how fast the
// number refills.
struct Stamina
{
    float current = 0.0f;
    float max_stamina = 0.0f;
    float recovery_timer = 0.0f;
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

// Marks the vermin. Areas hurt these; the exterminator is not one.
struct Vermin
{
    float contact_damage = 0.0f;
};
