#include "Footsteps.h"

#include "systems/AudioSystem.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <random>

namespace footsteps
{

void load(Config& cfg, const std::string& path)
{
    std::ifstream f(path);
    if (!f)
        return;
    const nlohmann::json j = nlohmann::json::parse(f, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded())
        return;
    cfg.volume = j.value("volume", cfg.volume);
    // Cadences gate the step timer; a 0 (or negative) value would fire a footstep every
    // frame (audio spam). Clamp to a small floor so a config typo can't spam SFX.
    cfg.walk_cadence = std::max(0.05f, j.value("walk_cadence", cfg.walk_cadence));
    cfg.run_cadence = std::max(0.05f, j.value("run_cadence", cfg.run_cadence));
    cfg.default_surface = j.value("default_surface", cfg.default_surface);
    // pools: { "Grass": [...], "Sand": [...], "Bridge": [...] } -- keyed by the surface
    // tag. A surface with no entry is silent (water).
    if (const auto it = j.find("pools"); it != j.end() && it->is_object())
        for (const auto& [surface, files] : it->items())
            if (files.is_array())
                cfg.pools[surface] = files.get<std::vector<std::string>>();
}

// An EMPTY surface name means "untagged tile" -> the default pool (bare ground sounds
// like grass). A NAMED surface resolves only to its own pool: if it has none, the step
// is silent BY INTENT (water is tagged but has no pool, so it stays silent instead of
// borrowing grass). Returns nullptr when silent.
const std::vector<std::string>* poolFor(const Config& cfg, const std::string& surface)
{
    const std::string& key = surface.empty() ? cfg.default_surface : surface;
    const auto it = cfg.pools.find(key);
    if (it != cfg.pools.end() && !it->second.empty())
        return &it->second;
    return nullptr;
}

bool tick(State& state, const Config& cfg, bool moving, bool run, float dt)
{
    if (!moving)
    {
        state.step_timer = 0.0f; // standing -- next step lands on the first moving frame
        return false;
    }
    state.step_timer -= dt;
    if (state.step_timer > 0.0f)
        return false;
    state.step_timer = run ? cfg.run_cadence : cfg.walk_cadence;
    return true;
}

void update(State& state, const Config& cfg, const std::string& surface, bool moving, bool run,
            float dt)
{
    if (!tick(state, cfg, moving, run, dt))
        return;
    const std::vector<std::string>* pool = poolFor(cfg, surface);
    if (!pool)
        return; // silent surface (water) -- no footfall
    static std::mt19937 rng{std::random_device{}()};
    const auto idx = std::uniform_int_distribution<std::size_t>{0, pool->size() - 1}(rng);
    AudioSystem::playSfx((*pool)[idx], cfg.volume);
}

} // namespace footsteps
