#include "systems/DamageSystem.h"

#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "ecs/GameComponents.h"
#include "ecs/FeelConfig.h"
#include "ops/GuideOps.h"
#include "ops/LootOps.h"
#include "ops/NavUtils.h"
#include "ops/RecordOps.h"
#include "renderers/FloaterRenderer.h"
#include "renderers/NotificationRenderer.h"
#include "screens/ScreenStyle.h"
#include "systems/CombatSystem.h"
#include "systems/DescentSystem.h"
#include "systems/PickupSystem.h"
#include "systems/PlayerSystem.h"
#include "systems/RewardSystem.h"
#include "systems/TintSystem.h"
#include "systems/WaveSystem.h"

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
// A corpse FADES rather than cutting out -- longer than the flash, so a kill pops bright and
// then ghosts away instead of vanishing between one frame and the next.

// Where `target` sits in the area's already-hurt ledger; the ledger's size when absent.
size_t markIndex(const HitArea& area, entt::entity target)
{
    for (size_t i = 0; i < area.hit.size(); ++i)
        if (area.hit[i].target == target)
            return i;
    return area.hit.size();
}

// Has this area already hurt `target`, and is it still too soon to do it again? A one-shot area
// never re-hits; a stream re-hits on its own interval, which is what makes holding the trigger on
// something actually kill it.
bool onCooldownFor(const HitArea& area, entt::entity target)
{
    const size_t i = markIndex(area, target);
    if (i == area.hit.size())
        return false;
    return area.rehit <= 0.0f || area.age < area.hit[i].next_at;
}

void markHit(HitArea& area, entt::entity target)
{
    const size_t i = markIndex(area, target);
    if (i == area.hit.size())
        area.hit.push_back(HitMark{target, area.age + area.rehit});
    else
        area.hit[i].next_at = area.age + area.rehit;
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
    return dot >= std::cos(geom::degToRad(area.arc));
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
        if (!reg.all_of<Pest>(target))
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
        floaters::add(t.x, t.y - 6.0f, std::to_string(dealt),
                      fatal ? screen_style::kCallout : screen_style::kDamage);

        // A hit flashes white briefly; a KILL flashes hot and holds longer, and the thing stays
        // on screen for it. The two have to look different, or clearing a crowd gives no
        // feedback about what actually died -- which is the only thing the player cares about.
        reg.emplace_or_replace<HitFlash>(target, HitFlash{tint::flashSeconds(fatal)});
        if (fatal)
            reg.emplace_or_replace<Dying>(target, Dying{feel::current().fade.death});
    }
}

// Age and move every live area, resolve what it hurts, and destroy the spent ones.
void tickAreas(entt::registry& reg, float dt)
{
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
}

// Flashes run down. What a flash LOOKS like belongs to the tint pass, which decides every
// colour in one place; this only says how much of one is left.
void tickFlashes(entt::registry& reg, float dt)
{
    std::vector<entt::entity> doneFlashing;
    for (auto [entity, flash] : reg.view<HitFlash>().each())
    {
        flash.remaining -= dt;
        if (flash.remaining <= 0.0f)
            doneFlashing.push_back(entity);
    }
    for (const auto e : doneFlashing)
        reg.remove<HitFlash>(e);
}

// Clear the dead, once they have finished dying. Here rather than in a system of its own:
// nothing else in the game reduces health yet, so this is the only place a thing can die.
void reapDead(EntityManager& em, float dt)
{
    auto& reg = em.registry();
    std::vector<entt::entity> dead;
    for (auto [entity, dying] : reg.view<Dying>().each())
    {
        dying.remaining -= dt;
        // The body dissolves as the timer runs down; by the time it is destroyed it is already
        // invisible, so the removal itself can never be seen.
        if (auto* spr = reg.try_get<Sprite>(entity))
            spr->alpha = std::max(0.0f, dying.remaining / feel::current().fade.death);
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
        // The hole it came out of counts it too: a program advances by what he
        // kills out of it, never by time spent or ground walked.
        swarm::countKill(em, e);

        // The kill goes on the record by species -- the one ledger everything later reads. The
        // tally reaching the guide's entry gate is announced through the feed: the book fills
        // in the field, not silently behind the pause screen.
        if (const auto* species = reg.try_get<Species>(e))
        {
            // Lazily on the first kill -- boot has settled the working directory by then --
            // and once: the gates are config, stable for the session.
            static const guide::Gates sGates = guide::gates("config/stats.json");
            const int had = record::kills(species->path);
            record::countKill(species->path);
            if (guide::tier(had, sGates) == guide::Tier::Undocumented &&
                guide::tier(had + 1, sGates) != guide::Tier::Undocumented)
                notify::line(species->path,
                             guide::nameOf(species->path) + " added to the field guide");
        }
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

} // namespace

void forget(EntityManager& em)
{
    em.registry().clear<HitFlash>();
}

void update(EntityManager& em, float dt)
{
    tickAreas(em.registry(), dt);
    tickFlashes(em.registry(), dt);
    reapDead(em, dt);
}

} // namespace hit_area
