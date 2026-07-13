#include "Footsteps.h"

#include "systems/AudioSystem.h"

#include <nlohmann/json.hpp>

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
    cfg.walk_cadence = j.value("walk_cadence", cfg.walk_cadence);
    cfg.run_cadence = j.value("run_cadence", cfg.run_cadence);
    if (const auto it = j.find("grass"); it != j.end() && it->is_array())
        cfg.grass = it->get<std::vector<std::string>>();
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

void update(State& state, const Config& cfg, bool moving, bool run, float dt)
{
    if (!tick(state, cfg, moving, run, dt) || cfg.grass.empty())
        return;
    static std::mt19937 rng{std::random_device{}()};
    const auto idx = std::uniform_int_distribution<std::size_t>{0, cfg.grass.size() - 1}(rng);
    AudioSystem::playSfx(cfg.grass[idx], cfg.volume);
}

} // namespace footsteps
