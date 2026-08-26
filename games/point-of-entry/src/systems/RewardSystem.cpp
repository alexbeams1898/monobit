#include "systems/RewardSystem.h"

#include "ecs/BalanceConfig.h"
#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "ecs/GameComponents.h"
#include "ops/LogUtils.h"
#include "renderers/FloaterRenderer.h"
#include "systems/DescentSystem.h"
#include "systems/PlayerSystem.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <entt/entt.hpp>

namespace reward
{
namespace
{

// Price of the next point: base * growth^(level-1). Geometric because each point buys the same
// amount of power -- a flat price would make the tenth point cheaper in effort than the first.
// Reads the SHEET for the exponent: level is points spent, so the sheet is the whole receipt.
int priceAt(int level)
{
    const auto& f = stats::formulas().reward;
    return static_cast<int>(std::lround(static_cast<float>(f.xp_base) *
                                        std::pow(f.xp_growth, static_cast<float>(level - 1))));
}

} // namespace

float rate()
{
    const int fronts = descent::frontsOpen();
    return fronts <= 1 ? 1.0f : 1.0f + static_cast<float>(fronts - 1) * stats::frontBonus();
}

void credit(EntityManager& em, int worth)
{
    if (worth <= 0)
        return;
    auto& reg = em.registry();
    const entt::entity p = player::entity();
    if (!reg.valid(p))
        return;
    reg.get_or_emplace<Earnings>(p, Earnings{}).banked +=
        std::max(1, static_cast<int>(std::lround(static_cast<float>(worth) * rate())));
}

void update(EntityManager& em, float dt)
{
    auto& reg = em.registry();
    const entt::entity p = player::entity();
    if (!reg.valid(p))
        return;

    // No passive mend: healing in the field is the thermos, and resting is an ACT (see
    // ThermosSystem::rest). Standing near the kit heals nothing by itself.
    (void)dt;
    (void)p;
}

int banked(const EntityManager& em)
{
    const entt::entity p = player::entity();
    if (!em.registry().valid(p) || !em.registry().all_of<Earnings>(p))
        return 0;
    return em.registry().get<Earnings>(p).banked;
}

int costOfNext(const EntityManager& em)
{
    const entt::entity p = player::entity();
    if (!em.registry().valid(p) || !em.registry().all_of<Stats>(p))
        return priceAt(1);
    return priceAt(stats::level(em.registry().get<Stats>(p)));
}

bool atRest(const EntityManager& em)
{
    const auto& reg = em.registry();
    const entt::entity p = player::entity();
    if (!reg.valid(p))
        return false;
    const auto& pt = reg.get<Transform>(p);
    for (const auto [e, t, spot] : reg.view<Transform, RestSpot>().each())
    {
        const float dx = pt.x - t.x;
        const float dy = pt.y - t.y;
        if (dx * dx + dy * dy < spot.radius * spot.radius)
            return true;
    }
    return false;
}

bool spend(EntityManager& em, int statIndex)
{
    auto& reg = em.registry();
    const entt::entity p = player::entity();
    if (!reg.valid(p) || !reg.all_of<Stats>(p) || statIndex < 0 || statIndex > 4)
        return false;
    // Points are sold at the rest spot and nowhere else -- that walk back is the design.
    if (!atRest(em))
        return false;
    auto& purse = reg.get_or_emplace<Earnings>(p, Earnings{});
    const int price = costOfNext(em);
    if (purse.banked < price)
        return false;
    purse.banked -= price;
    auto& s = reg.get<Stats>(p);
    int* fields[5] = {&s.chemical, &s.physical, &s.biological, &s.endurance, &s.inspection};
    ++(*fields[statIndex]);
    // The body follows the sheet immediately -- and keeps its fraction, so buying points is
    // never a heal. The heal belongs to RESTING, which is a different act.
    stats::applyDerivations(em, p);
    return true;
}

} // namespace reward
