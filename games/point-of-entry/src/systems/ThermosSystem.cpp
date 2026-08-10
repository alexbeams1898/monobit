#include "systems/ThermosSystem.h"

#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "ecs/GameComponents.h"
#include "ops/LogUtils.h"
#include "systems/PlayerSystem.h"
#include "systems/RewardSystem.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>

#include <entt/entt.hpp>

namespace thermos
{
namespace
{
std::vector<Fill> sFills;
int sFillIndex = 0;
int sSips = 0;
int sSipsMax = 4;
} // namespace

void load(const std::string& statsPath)
{
    sFills.clear();
    sFillIndex = 0;
    std::ifstream in(statsPath);
    const nlohmann::json j =
        in ? nlohmann::json::parse(in, nullptr, /*allow_exceptions=*/false) : nlohmann::json{};
    const nlohmann::json t = (!j.is_discarded() && j.is_object())
                                 ? j.value("thermos", nlohmann::json::object())
                                 : nlohmann::json::object();
    sSipsMax = t.value("sips", 4);
    const nlohmann::json paths = t.value("fills", nlohmann::json::array());
    for (const auto& path : paths)
    {
        if (!path.is_string())
            continue;
        std::ifstream f(path.get<std::string>());
        const nlohmann::json fj =
            f ? nlohmann::json::parse(f, nullptr, /*allow_exceptions=*/false) : nlohmann::json{};
        if (fj.is_discarded() || !fj.is_object())
        {
            poe::log().error("thermos: fill '{}' is missing or malformed", path.get<std::string>());
            continue;
        }
        Fill fill;
        fill.path = path.get<std::string>();
        fill.name = fj.value("name", std::string{"?"});
        fill.heal = fj.value("heal", fill.heal);
        fill.stamina = fj.value("stamina", fill.stamina);
        sFills.push_back(std::move(fill));
    }
    sSips = sSipsMax; // he leaves the van with a full thermos
    poe::log().info("thermos: {} fill(s), {} sips", sFills.size(), sSipsMax);
}

const std::vector<Fill>& fills()
{
    return sFills;
}

int fillIndex()
{
    return sFillIndex;
}

void setFill(EntityManager& em, int index)
{
    if (index < 0 || index >= static_cast<int>(sFills.size()))
        return;
    sFillIndex = index;
    // Standing at the staging area, a new brew is a refill; away from it, the choice waits.
    if (reward::atRest(em))
        sSips = sSipsMax;
}

int sipsLeft()
{
    return sSips;
}

int sipsMax()
{
    return sSipsMax;
}

bool sip(EntityManager& em)
{
    if (sSips <= 0 || sFills.empty())
        return false;
    auto& reg = em.registry();
    const entt::entity p = player::entity();
    if (!reg.valid(p))
        return false;
    auto* hp = reg.try_get<Health>(p);
    auto* sta = reg.try_get<Stamina>(p);
    const Fill& fill = sFills[static_cast<std::size_t>(sFillIndex)];
    // A sip at full health and full stamina would be wasted, and the flask refuses waste --
    // the genre's rule, and also just how a working man treats his coffee.
    const bool hpUseful = hp != nullptr && hp->current < hp->max;
    const bool staUseful = fill.stamina > 0.0f && sta != nullptr && sta->current < sta->max_stamina;
    if (!hpUseful && !staUseful)
        return false;
    if (hp != nullptr)
        hp->current = std::min(hp->max, hp->current + fill.heal);
    if (sta != nullptr && fill.stamina > 0.0f)
        sta->current = std::min(sta->max_stamina, sta->current + fill.stamina);
    --sSips;
    return true;
}

void rest(EntityManager& em)
{
    auto& reg = em.registry();
    const entt::entity p = player::entity();
    if (!reg.valid(p))
        return;
    if (auto* hp = reg.try_get<Health>(p))
        hp->current = hp->max;
    if (auto* sta = reg.try_get<Stamina>(p))
        sta->current = sta->max_stamina;
    sSips = sSipsMax;
    poe::log().info("thermos: rested -- refilled with {}",
                    sFills.empty() ? "nothing" : sFills[static_cast<std::size_t>(sFillIndex)].name);
}

} // namespace thermos
