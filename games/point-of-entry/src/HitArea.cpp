#include "HitArea.h"

#include "Combat.h"
#include "ecs/Components.h"
#include "ecs/EntityManager.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace hit_area
{
namespace
{

// A struck thing flashes for a moment; a dying one holds noticeably longer, which is what makes
// a kill legible in a crowd where several things are being hit at once.
constexpr float kHitFlash = 0.12f;
constexpr float kDeathFlash = 0.22f;

bool alreadyHit(const HitArea& area, entt::entity target)
{
    return std::find(area.hit.begin(), area.hit.end(), target) != area.hit.end();
}

// Everything inside the circle that has not been hurt by this area yet. Distance is compared
// squared: a square root per enemy per area per frame buys nothing when only the comparison
// matters, and a swarm makes that count.
void applyDamage(entt::registry& reg, HitArea& area, const Transform& at)
{
    for (auto [target, t, health] : reg.view<Transform, Health>().each())
    {
        if (target == area.owner || alreadyHit(area, target))
            continue;
        if (!reg.all_of<Vermin>(target))
            continue;
        // Already dying: a corpse is not a target. Without this a lingering cloud re-kills what
        // it has already killed every time a new area catches it, and anything that eventually
        // pays out for a kill would pay several times for one.
        if (reg.all_of<Dying>(target))
            continue;
        const float dx = t.x - at.x;
        const float dy = t.y - at.y;
        if (dx * dx + dy * dy > area.radius * area.radius)
            continue;
        // Rounded up, so a small area is never a free hit that does nothing at all.
        health.current -= std::max(1, static_cast<int>(std::lround(area.damage)));
        area.hit.push_back(target);

        // A hit flashes white briefly; a KILL flashes hot and holds longer, and the thing stays
        // on screen for it. The two have to look different, or clearing a crowd gives no
        // feedback about what actually died -- which is the only thing the player cares about.
        const bool killed = health.current <= 0;
        reg.emplace_or_replace<HitFlash>(target, HitFlash{killed ? kDeathFlash : kHitFlash});
        if (killed)
            reg.emplace_or_replace<Dying>(target, Dying{kDeathFlash});
    }
}

} // namespace

void update(EntityManager& em, float dt)
{
    auto& reg = em.registry();
    std::vector<entt::entity> expired;

    for (auto [entity, area, transform] : reg.view<HitArea, Transform>().each())
    {
        if (area.speed > 0.0f)
        {
            const float step = area.speed * dt;
            transform.x += area.dir_x * step;
            transform.y += area.dir_y * step;
            area.range_left -= step;
        }

        applyDamage(reg, area, transform);

        // A travelling area is spent by DISTANCE, a lingering one by time. Both are checked, so
        // a thrown cloud that also lingers behaves the way both words suggest.
        area.remaining -= dt;
        const bool spent = area.remaining <= 0.0f || (area.speed > 0.0f && area.range_left <= 0.0f);
        if (spent)
            expired.push_back(entity);
    }

    for (const auto e : expired)
        reg.destroy(e);

    // Flashes fade. TintOverride is what the renderer actually reads, so the flash both sets and
    // clears it -- a tint left behind would stain the thing white for the rest of its life.
    std::vector<entt::entity> doneFlashing;
    for (auto [entity, flash] : reg.view<HitFlash>().each())
    {
        flash.remaining -= dt;
        if (flash.remaining <= 0.0f)
            doneFlashing.push_back(entity);
        else if (reg.all_of<Dying>(entity))
        {
            // Blown out toward white and fading with the corpse, so a kill reads as a thing
            // going out rather than a thing being struck.
            const float t = flash.remaining / kDeathFlash;
            reg.emplace_or_replace<TintOverride>(
                entity, TintOverride{6.0f, 5.0f * t + 1.0f, 3.0f * t + 1.0f});
        }
        else
            reg.emplace_or_replace<TintOverride>(entity, TintOverride{2.5f, 2.5f, 2.5f});
    }
    for (const auto e : doneFlashing)
    {
        reg.remove<TintOverride>(e);
        reg.remove<HitFlash>(e);
    }

    // Clear the dead, once they have finished dying. Here rather than in a system of its own:
    // nothing else in the game reduces health yet, so this is the only place a thing can die.
    std::vector<entt::entity> dead;
    for (auto [entity, dying] : reg.view<Dying>().each())
    {
        dying.remaining -= dt;
        if (dying.remaining <= 0.0f)
            dead.push_back(entity);
    }
    for (const auto e : dead)
        reg.destroy(e);
}

} // namespace hit_area
