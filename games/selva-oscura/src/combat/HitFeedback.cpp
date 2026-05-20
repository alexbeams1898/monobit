#include "combat/HitFeedback.h"

#include <algorithm>
#include <random>

namespace selva::combat
{

namespace
{

std::vector<DamageNumber> sNumbers;
std::mt19937 sRng{0xD17AU}; // stable jitter seed

// Per-number constants. Soulslike-ish: drift up ~1.5m over a 1s
// lifetime so the number reads above the head before fading.
constexpr float kLifetime = 1.0f;
constexpr float kDriftHeight = 1.5f;   // total upward drift in meters
constexpr float kJitterRadius = 0.10f; // horizontal jitter so stacked hits don't overlap

} // namespace

void spawnDamageNumber(const glm::vec3& world_pos, int amount, bool crit)
{
    std::uniform_real_distribution<float> jitter(-kJitterRadius, kJitterRadius);
    DamageNumber d;
    d.origin = world_pos + glm::vec3(jitter(sRng), 0.0f, jitter(sRng));
    d.world_pos = d.origin;
    d.amount = amount;
    d.elapsed = 0.0f;
    d.lifetime = kLifetime;
    d.crit = crit;
    sNumbers.push_back(d);
}

void tickDamageNumbers(float dt)
{
    for (auto& d : sNumbers)
    {
        d.elapsed += dt;
        // Ease-out drift: fast at start, slows toward end.
        const float t = std::clamp(d.elapsed / d.lifetime, 0.0f, 1.0f);
        const float y_drift = kDriftHeight * (1.0f - (1.0f - t) * (1.0f - t));
        d.world_pos = d.origin + glm::vec3(0.0f, y_drift, 0.0f);
    }
    sNumbers.erase(std::remove_if(sNumbers.begin(), sNumbers.end(),
                                  [](const DamageNumber& d) { return d.elapsed >= d.lifetime; }),
                   sNumbers.end());
}

const std::vector<DamageNumber>& damageNumbers()
{
    return sNumbers;
}

} // namespace selva::combat
