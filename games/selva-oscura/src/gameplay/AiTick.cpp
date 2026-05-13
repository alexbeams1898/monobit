#include "gameplay/AiTick.h"

#include "Tunables.h"
#include "WallClock.h"
#include "gameplay/Actor.h"
#include "gameplay/Perception.h"

#include <algorithm>
#include <cstdint>

namespace selva::gameplay
{

namespace
{

// Deterministic small jitter generator. xorshift64-derived, seeded
// from the actor's spawn yaw bits + tick count proxy. Avoids
// std::mt19937 overhead per tick — we want microseconds of jitter
// so AI ticks don't lock-step over hours of play, not a real RNG.
float xorshiftJitter(std::uint32_t state)
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    // Map to [-0.5, +0.5].
    return (static_cast<float>(state) / static_cast<float>(0xFFFFFFFFu)) - 0.5f;
}

float effectiveHz(const Actor& actor, const selva::tuning::Tunables& tun)
{
    float hz = std::max(1.0f, tun.ai_decision_tick_hz);
    if (actor.perception.awareness == Awareness::Combat)
        hz *= std::max(0.1f, tun.ai_decision_tick_combat_hz_multiplier);
    return hz;
}

} // namespace

bool shouldTickAi(Actor& actor, const selva::tuning::Tunables& tun)
{
    const float now = selva::wallClock();
    if (now < actor.next_ai_tick_time)
        return false;

    const float hz = effectiveHz(actor, tun);
    const float period = 1.0f / hz;
    // Jitter is +/- 10% of the period. Hash the actor's spawn pos
    // bits as the xorshift seed; bit-twiddle so the same actor on
    // the same tick window doesn't produce identical jitter.
    const std::uint32_t seed =
        static_cast<std::uint32_t>(static_cast<std::int64_t>(now * 1000.0f)) ^
        static_cast<std::uint32_t>(static_cast<std::int64_t>(actor.spawn_pos.x * 1000.0f)) ^
        static_cast<std::uint32_t>(static_cast<std::int64_t>(actor.spawn_pos.z * 1000.0f));
    const float jitter = xorshiftJitter(seed) * 0.10f * period;
    // Schedule the next tick relative to its *target* time, not
    // `now`. The render frame typically fires the gate ~10ms after
    // the target due to render cadence; advancing from `now` would
    // accumulate that lateness each tick (10Hz drifts to ~8.5Hz).
    // Advancing from the previous target keeps the long-run rate
    // honest. If we fell so far behind that we're past two periods,
    // clamp to `now` to avoid burst-catch-up.
    actor.next_ai_tick_time += period + jitter;
    if (actor.next_ai_tick_time < now)
        actor.next_ai_tick_time = now + period + jitter;
    return true;
}

void seedAiTickPhase(Actor& actor, int pool_index, const selva::tuning::Tunables& tun)
{
    const float hz = std::max(1.0f, tun.ai_decision_tick_hz);
    const float period = 1.0f / hz;
    // Phase across the period by pool index — index 0 ticks at +0,
    // index 1 at +1/N of the period, etc. Modulo over N=10 (one full
    // period at 10Hz default) is plenty of spread; more actors than
    // that and we accept some clustering, jitter handles the rest.
    constexpr int kSlots = 10;
    const float slot_fraction =
        static_cast<float>(pool_index % kSlots) / static_cast<float>(kSlots);
    actor.next_ai_tick_time = selva::wallClock() + slot_fraction * period;
}

} // namespace selva::gameplay
