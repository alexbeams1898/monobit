#include "loot/Pickups.h"

#include "AppStateGlobal.h"
#include "Tunables.h"
#include "WallClock.h"
#include "anim/SkeletonJointMap.h"
#include "combat/ActorVolumes.h"
#include "gameplay/Actor.h"
#include "gameplay/Enemies.h"
#include "items/ItemRegistry.h"
#include "notice/Notices.h"
#include "ops/InventoryOps.h"
#include "ui/Notifications.h"

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include <cstdio>
#include <string>

namespace selva::loot
{

namespace
{

std::vector<Pickup>& pool()
{
    static std::vector<Pickup> p;
    return p;
}

Id nextId()
{
    static Id s_counter = 1;
    return s_counter++;
}

// Find a pickup by Id; nullptr if not found. Used by the interact
// callback to resolve which pickup fired without capturing a raw
// vector index (the vector mutates between spawn and interact).
Pickup* findById(Id id)
{
    for (auto& p : pool())
    {
        if (p.id == id)
            return &p;
    }
    return nullptr;
}

void removePickup(Id id)
{
    auto& v = pool();
    for (auto it = v.begin(); it != v.end(); ++it)
    {
        if (it->id == id)
        {
            selva::interact::unregisterInteractable(it->interact_id);
            v.erase(it);
            return;
        }
    }
}

void grantPickup(Id pickup_id)
{
    Pickup* p = findById(pickup_id);
    if (p == nullptr)
        return;

    PlayerProfile* profile = activePlayerProfile();
    if (profile == nullptr)
    {
        // Defensive -- player picked up an item with no active
        // profile (shouldn't happen during Playing). Leave the
        // pickup in the world for the next attempt.
        std::fprintf(stderr, "[pickup] no active profile; pickup %llu left in world\n",
                     static_cast<unsigned long long>(pickup_id));
        std::fflush(stderr);
        return;
    }

    const auto& registry = selva::items::itemRegistry();
    const engine::ecs::ItemDef* def = registry.find(p->item.config_path);
    const std::string display_name =
        (def != nullptr && !def->name.empty()) ? def->name : p->item.config_path;
    const engine::ecs::Rarity rarity =
        (def != nullptr) ? def->rarity : engine::ecs::Rarity::Common;

    // Compendium check BEFORE addItem so the toast can reflect
    // first-time-ever discovery vs repeat. discover() returns true
    // only on first-ever encounter for this character. Survives
    // across deaths within the run + persists to save per
    // [[project_selva_core_framing]] (vestigia remember).
    const bool is_new = profile->compendium.discover(p->item.config_path);

    // Mark the item as unread for the inventory UI dot. The
    // per-instance ItemInstance::newly_discovered field is engine-
    // layer state we leave at its default (engine refactor concern,
    // not selva's); selva reads selva::notice::isUnread instead so
    // future "you have new things" UIs share one storage layer.
    if (is_new)
        selva::notice::mark(selva::notice::kDomainItem, p->item.config_path);

    const auto granted_id =
        engine::ops::inventory::addItem(profile->inventory, p->item, registry);
    if (granted_id == engine::ecs::kInvalidItemInstanceId)
    {
        std::fprintf(stderr, "[pickup] grant rejected for '%s'; item not in registry\n",
                     p->item.config_path.c_str());
        std::fflush(stderr);
        return;
    }

    // Toast: gold for first-time-ever discovery, neutral light gray
    // for repeats. The "NEW!" suffix is meta-frame UI language per
    // [[project_voice_doctrine_unified_commedia]] (the voice doctrine
    // exempts mechanical / UI registers).
    char buf[160];
    if (is_new)
    {
        std::snprintf(buf, sizeof(buf), "+%d %s (NEW!)", p->item.quantity, display_name.c_str());
        const glm::vec4 gold{0.95f, 0.85f, 0.40f, 1.0f};
        selva::ui::pushNotification(buf, gold,
                                    def != nullptr ? def->icon_path : std::string{});
    }
    else
    {
        std::snprintf(buf, sizeof(buf), "+%d %s", p->item.quantity, display_name.c_str());
        const glm::vec4 neutral{0.85f, 0.85f, 0.85f, 1.0f};
        selva::ui::pushNotification(buf, neutral,
                                    def != nullptr ? def->icon_path : std::string{});
    }

    std::fprintf(stderr, "[pickup] +%d %s%s rarity=%d\n", p->item.quantity,
                 display_name.c_str(), is_new ? " NEW!" : "", static_cast<int>(rarity));
    std::fflush(stderr);

    // Capture the on_granted hook BEFORE removePickup destroys the
    // Pickup entry; fire AFTER remove so the hook can observe the
    // post-grant pool state cleanly (e.g. a dispenser that re-checks
    // whether the pickup is still in the world to know it was taken).
    auto hook = std::move(p->on_granted);

    // Erase the pickup AFTER printing -- removePickup unregisters the
    // interactable so the prompt clears next frame.
    removePickup(pickup_id);

    if (hook)
        hook();
}

} // namespace

Id spawnPickup(const glm::vec3& world_pos, const engine::ecs::ItemInstance& item,
               const std::string& source_actor_id, OnGranted on_granted)
{
    const auto& registry = selva::items::itemRegistry();
    const engine::ecs::ItemDef* def = registry.find(item.config_path);
    if (def == nullptr)
    {
        std::fprintf(stderr, "[pickup] spawn rejected for '%s'; item not in registry\n",
                     item.config_path.c_str());
        std::fflush(stderr);
        return kInvalidId;
    }

    Pickup p;
    p.id = nextId();
    // Cache world_pos as a fallback for when the source actor goes
    // away mid-frame; for source-bound pickups the live position is
    // resolved from the hips joint at render time.
    p.world_pos = world_pos;
    p.item = item;
    p.rarity = def->rarity;
    p.quality = item.quality;
    p.source_actor_id = source_actor_id;
    p.on_granted = std::move(on_granted);

    // Register an interactable BEFORE pushing into the pool so the
    // closure captures a stable pickup_id. The position closure
    // re-reads the pool entry each frame so future move semantics
    // (e.g. magnet-toward-player) won't need a re-register.
    const Id pickup_id = p.id;
    const std::string label = (def->name.empty()) ? def->config_path : def->name;
    selva::interact::Decl decl;
    decl.kind = selva::interact::Kind::Pickup;
    decl.label = label;
    decl.range_meters = 2.5f;
    decl.position = [pickup_id]() -> glm::vec3 {
        if (Pickup* pp = findById(pickup_id))
            return livePickupPos(*pp);
        return glm::vec3{0.0f, 0.0f, 0.0f};
    };
    decl.on_interact = [pickup_id]() { grantPickup(pickup_id); };
    p.interact_id = selva::interact::registerInteractable(std::move(decl));

    pool().push_back(std::move(p));
    return pickup_id;
}

const std::vector<Pickup>& allPickups()
{
    return pool();
}

glm::vec3 livePickupPos(const Pickup& p)
{
    // Unbound pickups (world-container drops, etc.): static position.
    if (p.source_actor_id.empty())
        return p.world_pos;

    const auto* src = selva::gameplay::actorByDeclId(p.source_actor_id);
    if (src == nullptr)
        return p.world_pos;

    // Resolve the actor's hips joint each frame. The X_Bot rig + the
    // wolf rig both declare a `hips` slot in their joint map; if for
    // some reason the lookup fails (no skeleton mapped, no hips slot,
    // joint index out of range) we fall back to the actor's world
    // origin + collider half-height so the glow at least stays
    // attached to the body anchor rather than jumping to (0,0,0).
    //
    // NOTE: we DON'T use PoseSampler::jointWorldPosWithActor here even
    // though it would be more direct. That accessor reads a cached
    // `actor_world_pos` that is only kept current for the player's
    // sampler (PerFrameTick calls setActorPlacement on sPlayer alone);
    // every enemy sampler's cache stays at (0,0,0) so the result comes
    // out near the world origin. Combat code (HitVolumes.cpp,
    // ActorVolumes.cpp) hits the same gap and works around it the same
    // way -- build the actor's model matrix from pos+yaw+foot_offset
    // and transform the joint-local position with it. Mirror that
    // here rather than waiting on the engine-side fix.
    const auto& jmap = selva::anim::jointMapByKey(src->skeleton_id);
    glm::vec3 anchor{src->pos.x, src->pos.y + src->body.collider_height * 0.5f,
                     src->pos.z};
    if (!jmap.hips.empty())
    {
        const int joint_idx = src->sampler.findJoint(jmap.hips.c_str());
        if (joint_idx >= 0)
        {
            const float foot_offset = selva::gameplay::actorFootOffsetY(*src);
            const glm::mat4 model_mat =
                selva::combat::buildActorModelMatrix(src->pos, src->yaw, foot_offset);
            const glm::vec3 joint_local = src->sampler.jointWorldPos(joint_idx);
            anchor = glm::vec3(model_mat * glm::vec4(joint_local, 1.0f));
        }
    }

    return anchor;
}

void hardReset()
{
    auto& v = pool();
    for (const auto& p : v)
        selva::interact::unregisterInteractable(p.interact_id);
    v.clear();
}

void tickPickups()
{
    // Pickups belong to their source corpse: when the corpse vanishes
    // from the actor pool, the pickup vanishes with it. Universal
    // rule per the items-loot doctrine -- handles soul-larvae
    // feeder-eats-corpse, future Hell-side dissolve-on-second-death,
    // and any other "corpse disappears" mechanic without per-archetype
    // configuration. Pickups with empty source_actor_id (e.g. future
    // world-container drops) skip this and persist indefinitely.
    auto& v = pool();
    if (v.empty())
        return;

    const auto& tun = selva::tuning::current();
    const float fade_window = tun.enemy_death_fade_hold_seconds +
                              tun.enemy_death_fade_duration_seconds;
    const float now = selva::wallClock();

    // Collect Ids to drop -- doing it in two passes keeps removePickup
    // (which iterates+erases) safe from concurrent-mutation issues.
    std::vector<Id> to_drop;
    for (const auto& p : v)
    {
        if (p.source_actor_id.empty())
            continue;
        const auto* a = selva::gameplay::actorByDeclId(p.source_actor_id);
        if (a == nullptr)
        {
            // Source recycled out of the actor pool entirely.
            to_drop.push_back(p.id);
            continue;
        }
        if (a->is_dead && a->death_time > 0.0f &&
            (now - a->death_time) >= fade_window)
        {
            // Source corpse has run its full fade and vanished from
            // render. Pickup follows.
            to_drop.push_back(p.id);
        }
    }

    for (Id id : to_drop)
        removePickup(id);
}

} // namespace selva::loot
