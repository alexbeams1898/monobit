#include "Stats.h"

#include "Combat.h"
#include "Log.h"
#include "ecs/Components.h"
#include "ecs/EntityManager.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>

#include <entt/entt.hpp>

namespace stats
{
namespace
{
Formulas sFormulas;
Stats sPlayerStart;
} // namespace

bool load(const std::string& path)
{
    std::ifstream in(path);
    if (!in)
    {
        poe::log().warn("stats: no config at '{}' -- using built-in formulas", path);
        return false;
    }
    const nlohmann::json j = nlohmann::json::parse(in, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded())
    {
        poe::log().error("stats: '{}' is not valid JSON -- using built-in formulas", path);
        return false;
    }
    const auto& h = j.value("health", nlohmann::json::object());
    sFormulas.health.base = h.value("base", sFormulas.health.base);
    sFormulas.health.endurance_scale = h.value("endurance_scale", sFormulas.health.endurance_scale);
    sFormulas.health.physical_scale = h.value("physical_scale", sFormulas.health.physical_scale);
    const auto& st = j.value("stamina", nlohmann::json::object());
    sFormulas.stamina.base = st.value("base", sFormulas.stamina.base);
    sFormulas.stamina.endurance_scale =
        st.value("endurance_scale", sFormulas.stamina.endurance_scale);
    sFormulas.stamina.recovery_delay = st.value("recovery_delay", sFormulas.stamina.recovery_delay);
    sFormulas.stamina.recovery_rate = st.value("recovery_rate", sFormulas.stamina.recovery_rate);
    const auto& d = j.value("defense", nlohmann::json::object());
    sFormulas.defense.level_scale = d.value("level_scale", sFormulas.defense.level_scale);
    sFormulas.defense.physical_scale = d.value("physical_scale", sFormulas.defense.physical_scale);
    sFormulas.defense.endurance_scale =
        d.value("endurance_scale", sFormulas.defense.endurance_scale);
    const auto& sc = j.value("scaling", nlohmann::json::object());
    sFormulas.scaling.per_point = sc.value("per_point", sFormulas.scaling.per_point);
    const auto& pl = j.value("player", nlohmann::json::object());
    sPlayerStart.chemical = pl.value("chemical", 1);
    sPlayerStart.physical = pl.value("physical", 1);
    sPlayerStart.biological = pl.value("biological", 1);
    sPlayerStart.endurance = pl.value("endurance", 1);
    sPlayerStart.inspection = pl.value("inspection", 1);
    return true;
}

const Stats& playerStart()
{
    return sPlayerStart;
}

const Formulas& formulas()
{
    return sFormulas;
}

int level(const Stats& s)
{
    return (s.chemical - 1) + (s.physical - 1) + (s.biological - 1) + (s.endurance - 1) +
           (s.inspection - 1) + 1;
}

int maxHealth(const Stats& s)
{
    const auto& f = sFormulas.health;
    return static_cast<int>(f.base + f.endurance_scale * static_cast<float>(s.endurance - 1) +
                            f.physical_scale * static_cast<float>(s.physical - 1));
}

float maxStamina(const Stats& s)
{
    const auto& f = sFormulas.stamina;
    return f.base + f.endurance_scale * static_cast<float>(s.endurance - 1);
}

int defense(const Stats& s)
{
    const auto& f = sFormulas.defense;
    return static_cast<int>(f.level_scale * static_cast<float>(level(s)) +
                            f.physical_scale * static_cast<float>(s.physical - 1) +
                            f.endurance_scale * static_cast<float>(s.endurance - 1));
}

void applyDerivations(EntityManager& em, entt::entity entity)
{
    auto& reg = em.registry();
    if (!reg.all_of<Stats>(entity))
        return;
    const auto& s = reg.get<Stats>(entity);

    // Keep the FRACTION across a re-derive: levelling up mid-fight should not silently heal to
    // full, and it must never shrink current below what the new maximum allows.
    const int newMaxHp = maxHealth(s);
    auto& hp = reg.get_or_emplace<Health>(entity, Health{newMaxHp, newMaxHp});
    const float hpFrac =
        hp.max > 0 ? static_cast<float>(hp.current) / static_cast<float>(hp.max) : 1.0f;
    hp.max = newMaxHp;
    hp.current = std::min(newMaxHp, static_cast<int>(std::lround(hpFrac * newMaxHp)));

    const float newMaxSta = maxStamina(s);
    auto& sta = reg.get_or_emplace<Stamina>(entity, Stamina{newMaxSta, newMaxSta});
    const float staFrac = sta.max_stamina > 0.0f ? sta.current / sta.max_stamina : 1.0f;
    sta.max_stamina = newMaxSta;
    sta.current = std::min(newMaxSta, staFrac * newMaxSta);
}

} // namespace stats
