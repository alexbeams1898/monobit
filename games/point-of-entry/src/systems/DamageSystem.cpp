#include "systems/DamageSystem.h"

#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "ecs/GameComponents.h"
#include "ops/LootOps.h"
#include "renderers/FloaterRenderer.h"
#include "systems/CombatSystem.h"
#include "systems/PickupSystem.h"
#include "systems/PlayerSystem.h"
#include "systems/RewardSystem.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

namespace hit_area
{
namespace
{

// A struck thing flashes for a moment; a dying one holds noticeably longer, which is what makes
// a kill legible in a crowd where several things are being hit at once.
constexpr float kHitFlash = 0.12f;
constexpr float kDeathFlash = 0.22f;
// A corpse FADES rather than cutting out -- longer than the flash, so a kill pops bright and
// then ghosts away instead of vanishing between one frame and the next.
constexpr float kDeathFade = 0.4f;

// Has this area already hurt `target`, and is it still too soon to do it again? A one-shot area
// never re-hits; a stream re-hits on its own interval, which is what makes holding the trigger on
// something actually kill it.
bool onCooldownFor(const HitArea& area, entt::entity target)
{
    for (size_t i = 0; i < area.hit.size(); ++i)
        if (area.hit[i] == target)
            return area.rehit <= 0.0f || area.age < area.hit_at[i];
    return false;
}

void markHit(HitArea& area, entt::entity target)
{
    for (size_t i = 0; i < area.hit.size(); ++i)
        if (area.hit[i] == target)
        {
            area.hit_at[i] = area.age + area.rehit;
            return;
        }
    area.hit.push_back(target);
    area.hit_at.push_back(area.age + area.rehit);
}

// Inside the cone? A zero arc means the area is a full circle and everything in range qualifies.
bool withinArc(const HitArea& area, float dx, float dy)
{
    if (area.arc <= 0.0f)
        return true;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len <= 0.0001f)
        return true; // standing on top of it counts, whatever the angle says
    const float dot = (dx * area.dir_x + dy * area.dir_y) / len;
    return dot >= std::cos(area.arc * 3.14159265f / 180.0f);
}

// Everything inside the circle that has not been hurt by this area yet. Distance is compared
// squared: a square root per enemy per area per frame buys nothing when only the comparison
// matters, and a swarm makes that count.
void applyDamage(entt::registry& reg, HitArea& area, const Transform& at)
{
    for (auto [target, t, health] : reg.view<Transform, Health>().each())
    {
        if (target == area.owner || onCooldownFor(area, target))
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
        // Only as far as the front has swept. min() so a long-held stream is simply its full
        // cone, not an ever-growing one.
        const float reach =
            area.expand > 0.0f ? std::min(area.radius, area.age * area.expand) : area.radius;
        if (dx * dx + dy * dy > reach * reach)
            continue;
        if (!withinArc(area, dx, dy))
            continue;
        // Rounded up, so a small area is never a free hit that does nothing at all.
        const int dealt = std::max(1, static_cast<int>(std::lround(area.damage)));
        health.current -= dealt;
        markHit(area, target);
        // The number is how a player tells a good hit from a bad one. A kill is called out in
        // white so the last hit on a thing does not look like every other hit.
        const bool fatal = health.current <= 0;
        floaters::add(t.x, t.y - 6.0f, std::to_string(dealt), fatal ? 1.0f : 0.95f,
                      fatal ? 1.0f : 0.85f, fatal ? 1.0f : 0.4f);

        // A hit flashes white briefly; a KILL flashes hot and holds longer, and the thing stays
        // on screen for it. The two have to look different, or clearing a crowd gives no
        // feedback about what actually died -- which is the only thing the player cares about.
        const bool killed = fatal;
        reg.emplace_or_replace<HitFlash>(target, HitFlash{killed ? kDeathFlash : kHitFlash});
        if (killed)
            reg.emplace_or_replace<Dying>(target, Dying{kDeathFade});
    }
}

} // namespace

void update(EntityManager& em, float dt)
{
    auto& reg = em.registry();
    std::vector<entt::entity> expired;

    for (auto [entity, area, transform] : reg.view<HitArea, Transform>().each())
    {
        area.age += dt;
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
        // The body dissolves as the timer runs down; by the time it is destroyed it is already
        // invisible, so the removal itself can never be seen.
        if (auto* spr = reg.try_get<Sprite>(entity))
            spr->alpha = std::max(0.0f, dying.remaining / kDeathFade);
        if (dying.remaining <= 0.0f)
            dead.push_back(entity);
    }
    for (const auto e : dead)
    {
        // What he kills goes back in the tank, and its worth hits the floor. Here rather than
        // at the moment of the fatal blow: a thing is only really dead once it has finished
        // dying, and paying at both points would pay twice.
        tools::creditKill(em);
        if (const auto* worth = reg.try_get<Worth>(e))
            reward::credit(em, worth->xp);
        // Goods hit the floor where it died; the roll is the species' table against his
        // Inspection. One rng for the whole game's drops -- reseeding per kill would make a
        // wave's loot correlate with its spawn pattern.
        if (const auto* table = reg.try_get<DropTable>(e))
            if (const auto* t = reg.try_get<Transform>(e))
            {
                static std::mt19937 sRng{std::random_device{}()};
                const auto* sheet = reg.try_get<Stats>(player::entity());
                const int insp = sheet != nullptr ? sheet->inspection : 1;
                for (const auto& inst : loot::roll(table->entries, insp, sRng))
                    pickup::spawnDrop(em, t->x, t->y, inst);
            }
        reg.destroy(e);
    }
}

} // namespace hit_area
