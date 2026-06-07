#include "gameplay/SanguePulse.h"

#include "AppStateGlobal.h"
#include "gameplay/RomanTiers.h"
#include "gameplay/Sangue.h"

#include <algorithm>
#include <random>

namespace selva::gameplay
{

namespace
{

std::vector<SanguePulse> sPulses;
std::mt19937 sRng{0x5A45A1Bu}; // stable seed for stagger jitter

struct PendingDrop
{
    glm::vec3 world_pos;
    std::uint32_t amount;
};

std::vector<PendingDrop> sPendingDrops;

// Per-particle flight duration (seconds) once delay elapses. Constant
// across tiers: a 100k particle and a 1 particle both take the same
// time to traverse the screen. What differs is visual weight.
constexpr float kParticleFlightSeconds = 0.55f;

// Per-particle stagger window scales with particle count so a small
// kill delivers ~instantly and a big stream lasts about half a second.
// Cap so a giant decomposition (3999 = 18 particles) doesn't run for
// 2 seconds -- still feels like one event.
float staggerWindow(std::size_t particle_count)
{
    constexpr float kPerParticle = 0.04f; // 40ms between particles
    constexpr float kMax = 0.65f;         // hard cap on stream length
    return std::min(kMax, static_cast<float>(particle_count) * kPerParticle);
}

} // namespace

void queueSangueDrop(const glm::vec3& corpse_world_pos, std::uint32_t amount)
{
    if (amount == 0u)
        return;
    sPendingDrops.push_back(PendingDrop{corpse_world_pos, amount});
}

void materializePendingDrops(const glm::vec2& target_screen_pos, WorldToScreenFn project, void* ctx)
{
    for (const auto& d : sPendingDrops)
    {
        glm::vec2 source_screen{target_screen_pos};
        if (project != nullptr)
            project(d.world_pos, source_screen, ctx);
        spawnSanguePulses(d.world_pos, source_screen, target_screen_pos, d.amount);
    }
    sPendingDrops.clear();
}

void clearPendingDropsForTest()
{
    sPendingDrops.clear();
}

void spawnSanguePulses(const glm::vec3& /*corpse_world_pos*/, const glm::vec2& source_screen_pos,
                       const glm::vec2& target_screen_pos, std::uint32_t amount)
{
    if (amount == 0u)
        return;
    const auto particles = decomposeAmountToParticles(amount);
    if (particles.empty())
        return;
    const float window = staggerWindow(particles.size());
    std::uniform_real_distribution<float> jitter(0.0f, window);
    for (const auto& spec : particles)
    {
        SanguePulse p;
        p.source_screen = source_screen_pos;
        p.target_screen = target_screen_pos;
        p.delay = (particles.size() == 1) ? 0.0f : jitter(sRng);
        p.lifetime = kParticleFlightSeconds;
        p.denomination = spec.denomination;
        p.tier_index = spec.tier_index;
        sPulses.push_back(p);
    }
}

std::size_t tickSanguePulsesWith(float dt, const glm::vec2& target_screen_pos,
                                 void (*grant)(std::uint32_t amount, void* ctx), void* ctx)
{
    std::size_t delivered_count = 0;
    for (auto& p : sPulses)
    {
        if (p.delivered)
            continue;
        p.elapsed += dt;
        // Re-anchor target each frame so the renderer follows the
        // current HUD layout (resize, vessel reposition, etc.).
        p.target_screen = target_screen_pos;
        // Delay phase: hold at source until delay elapses, then the
        // flight clock starts. We model this by deferring arrival
        // logic until elapsed > (delay + lifetime).
        if (p.elapsed >= p.delay + p.lifetime)
        {
            p.delivered = true;
            if (grant != nullptr)
                grant(p.denomination, ctx);
            ++delivered_count;
        }
    }
    sPulses.erase(std::remove_if(sPulses.begin(), sPulses.end(),
                                 [](const SanguePulse& p) { return p.delivered; }),
                  sPulses.end());
    return delivered_count;
}

namespace
{
void productionGrant(std::uint32_t amount, void* /*ctx*/)
{
    if (PlayerProfile* profile = activePlayerProfile())
        selva::sangue::grantOnKill(*profile, amount);
}
} // namespace

void tickSanguePulses(float dt, const glm::vec2& live_target_screen_pos)
{
    tickSanguePulsesWith(dt, live_target_screen_pos, &productionGrant, nullptr);
}

const std::vector<SanguePulse>& sanguePulses()
{
    return sPulses;
}

void clearSanguePulsesForTest()
{
    sPulses.clear();
}

} // namespace selva::gameplay
