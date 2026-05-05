#include "systems/HitboxResolverSystem.h"

#include "ecs/BalanceConfig.h"
#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "ecs/GameComponents.h"
#include "geom/Intersect.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <tracy/Tracy.hpp>

namespace
{

// Monotonically advancing game time accumulated each tick. Used to age out
// hit_history entries so multi-hit attacks can re-arm per interval.
static float sGameTime = 0.0f;

// A single weapon shape transformed into world space for this tick.
struct TransformedShape
{
    CollisionShape shape; // now in world-relative coords
    float dmg_mult = 1.0f;
    int priority = 0;
    int weapon_shape_index = 0;
};

// Rotate a 2D point by an angle (radians).
static void rotate(float& x, float& y, float cosA, float sinA)
{
    const float nx = x * cosA - y * sinA;
    const float ny = x * sinA + y * cosA;
    x = nx;
    y = ny;
}

// Transform a weapon-local shape into world space given attacker position,
// facing, and the per-frame keyframe offset + rotation.
static CollisionShape transformShape(const CollisionShape& in, float ax, float ay, float facing_cos,
                                      float facing_sin, float kf_x, float kf_y, float kf_rot)
{
    CollisionShape out = in;
    const float kf_cos = std::cos(kf_rot);
    const float kf_sin = std::sin(kf_rot);

    // Apply keyframe rotation to shape-internal offset vector for capsules.
    // For AABB/Circle, the "shape" position is at (x, y); for capsule also
    // (x2, y2). We rotate these by keyframe rot, then add keyframe offset,
    // then rotate by facing, then add attacker world pos.
    auto xformPoint = [&](float& px, float& py)
    {
        // 1. Apply keyframe rotation to the point (local to the weapon's icon
        //    space, which is anchored at 0,0 here).
        rotate(px, py, kf_cos, kf_sin);
        // 2. Apply keyframe offset in attacker-local space.
        px += kf_x;
        py += kf_y;
        // 3. Rotate by attacker facing.
        rotate(px, py, facing_cos, facing_sin);
        // 4. Translate to attacker world position.
        px += ax;
        py += ay;
    };

    xformPoint(out.x, out.y);
    if (out.kind == ShapeKind::Capsule)
        xformPoint(out.x2, out.y2);

    // Note: the shape is now positioned in world space by its x/y fields.
    // Callers pass (0, 0) as the "origin" to geom::overlaps.
    return out;
}

// Remove hit_history entries older than `now - window`.
static void expireHitHistory(Hurtbox& hb, float now, float window)
{
    auto& hist = hb.hit_history;
    hist.erase(std::remove_if(hist.begin(), hist.end(),
                              [&](const HitRecord& r)
                              { return now - r.last_hit_time > window; }),
               hist.end());
}

// True if the hurtbox was already hit by this attack_id within the re-arm
// window (single-hit attacks use infinite window by default).
static bool alreadyHit(const Hurtbox& hb, uint64_t attack_id, float now, float interval)
{
    for (const auto& r : hb.hit_history)
    {
        if (r.attack_id != attack_id)
            continue;
        if (interval < 0.0f)
            return true; // single-hit: already hit once, never again
        if (now - r.last_hit_time < interval)
            return true; // multi-hit: within re-arm window
    }
    return false;
}

static void recordHit(Hurtbox& hb, uint64_t attack_id, float now)
{
    for (auto& r : hb.hit_history)
    {
        if (r.attack_id == attack_id)
        {
            r.last_hit_time = now;
            return;
        }
    }
    hb.hit_history.push_back({attack_id, now});
}

// Read the correct Weapon component based on which hand is attacking.
static const Weapon* getAttackingWeapon(entt::registry& reg, entt::entity attacker, bool left_hand)
{
    if (left_hand)
        return reg.try_get<LeftWeapon>(attacker);
    return reg.try_get<Weapon>(attacker);
}

// Find the keyframe for the current animation frame, or nullptr if none.
static const HitboxKeyframe* findKeyframe(const AttackAnimHitbox& data, int frame_index)
{
    for (const auto& kf : data.keyframes)
    {
        if (kf.frame == frame_index)
            return &kf;
    }
    return nullptr;
}

// Run the resolver for one attacker with an active AttackLocked.
static void resolveForAttacker(EntityManager& em, entt::entity attacker, const AttackLocked& lock,
                                const AttackAnimHitboxData& anim_data)
{
    auto& reg = em.registry();

    // Need the weapon + transform + animation.
    const Weapon* weapon = getAttackingWeapon(reg, attacker, lock.left_hand);
    if (weapon == nullptr || weapon->hitboxes.empty() || weapon->attack_anim.empty())
        return;

    const auto* tf = reg.try_get<Transform>(attacker);
    const auto* anim = reg.try_get<Animation>(attacker);
    const auto* facing = reg.try_get<FacingDirection>(attacker);
    if (tf == nullptr || anim == nullptr || facing == nullptr)
        return;

    // Look up the attack-anim keyframes by name.
    const auto attackIt = anim_data.attacks.find(weapon->attack_anim);
    if (attackIt == anim_data.attacks.end())
        return;
    const AttackAnimHitbox& entry = attackIt->second;

    const HitboxKeyframe* kf = findKeyframe(entry, anim->frame_index);
    if (kf == nullptr)
        return;

    // Attacker facing angle (from facing dx/dy).
    const float face_angle = std::atan2(facing->dy, facing->dx);
    const float face_cos = std::cos(face_angle);
    const float face_sin = std::sin(face_angle);

    // Transform all weapon shapes into world space for this frame.
    std::vector<TransformedShape> world_shapes;
    world_shapes.reserve(weapon->hitboxes.size());
    for (int i = 0; i < static_cast<int>(weapon->hitboxes.size()); ++i)
    {
        const auto& whs = weapon->hitboxes[i];
        TransformedShape ts;
        ts.shape = transformShape(whs.shape, tf->x, tf->y, face_cos, face_sin, kf->x, kf->y,
                                   kf->rotation);
        ts.dmg_mult = whs.dmg_mult;
        ts.priority = whs.priority;
        ts.weapon_shape_index = i;
        world_shapes.push_back(ts);
    }

    // Test each hurtbox-owning entity against our transformed shapes.
    for (auto [target, targetTf, targetHb] : reg.view<Transform, Hurtbox>().each())
    {
        if (target == attacker)
            continue;
        if (!reg.all_of<Health>(target))
            continue;
        if (reg.all_of<Dead>(target))
            continue;

        // Expire stale hit history, then check dedup.
        // For multi-hit, window = hit_interval; for single-hit, arbitrary large.
        const float expire_window = entry.hit_interval > 0.0f ? entry.hit_interval : 10.0f;
        expireHitHistory(targetHb, sGameTime, expire_window);
        if (alreadyHit(targetHb, lock.attack_id, sGameTime, entry.hit_interval))
            continue;

        // Against each hurtbox shape, test all transformed weapon shapes, take
        // the highest-priority one that overlaps. This is the per-hurtbox-shape
        // winner; track across all hurtbox shapes and pick the single best hit.
        int best_hurt_shape = -1;
        int best_weapon_shape = -1;
        int best_priority = std::numeric_limits<int>::min();
        float best_dmg_mult = 1.0f;

        const float targetScale = targetTf.scale;
        for (int hi = 0; hi < static_cast<int>(targetHb.shapes.size()); ++hi)
        {
            // Scale the hurtbox shape by the target's transform.scale so
            // bigger characters have proportionally bigger hit regions.
            CollisionShape scaledHurt = targetHb.shapes[hi].shape;
            scaledHurt.x *= targetScale;
            scaledHurt.y *= targetScale;
            scaledHurt.w *= targetScale;
            scaledHurt.h *= targetScale;
            scaledHurt.r *= targetScale;
            scaledHurt.x2 *= targetScale;
            scaledHurt.y2 *= targetScale;
            for (const auto& ws : world_shapes)
            {
                // ws.shape is in world coords, pass (0, 0) as world origin.
                // scaledHurt is in target-local coords, pass target's world pos.
                if (!geom::overlaps(ws.shape, 0.0f, 0.0f, scaledHurt, targetTf.x, targetTf.y))
                    continue;
                if (ws.priority > best_priority)
                {
                    best_priority = ws.priority;
                    best_weapon_shape = ws.weapon_shape_index;
                    best_hurt_shape = hi;
                    best_dmg_mult = ws.dmg_mult * targetHb.shapes[hi].dmg_mult;
                }
            }
        }

        if (best_hurt_shape < 0)
            continue;

        // Emit the hit: record + push a collision event. We create a one-shot
        // synthetic hitbox entity that carries the damage + hit_shape_index
        // so DamageSystem's existing pipeline consumes it. This avoids
        // restructuring DamageSystem; HitboxResolverSystem is the new source.
        const auto hitboxEnt = em.create();
        Hitbox hb{};
        hb.damage = weapon->base_damage * best_dmg_mult;
        hb.owner = attacker;
        hb.hit_something = false;
        hb.left_hand = lock.left_hand;
        hb.hit_shape_index = best_hurt_shape;
        reg.emplace<Hitbox>(hitboxEnt, hb);
        reg.emplace<PendingDestroy>(hitboxEnt); // cleaned up next tick by Projectile path

        em.collision_events.push_back({hitboxEnt, target});

        recordHit(targetHb, lock.attack_id, sGameTime);
    }
}

} // namespace

namespace HitboxResolverSystem
{

void update(EntityManager& em, float dt)
{
    ZoneScopedN("HitboxResolverSystem");
    sGameTime += dt;

    auto& reg = em.registry();

    // Clean up synthetic hitbox entities we created last tick. These have
    // Hitbox + PendingDestroy but are NOT projectiles. ProjectileSystem
    // handles its own PendingDestroy cleanup separately.
    {
        std::vector<entt::entity> expired;
        for (auto [e, tag] : reg.view<Hitbox, PendingDestroy>().each())
        {
            (void)tag;
            if (!reg.all_of<Projectile>(e))
                expired.push_back(e);
        }
        for (auto e : expired)
            em.destroy(e);
    }

    const auto* anim_data = reg.ctx().find<AttackAnimHitboxData>();
    if (anim_data == nullptr)
        return;

    for (auto [attacker, lock] : reg.view<AttackLocked>().each())
        resolveForAttacker(em, attacker, lock, *anim_data);
}

} // namespace HitboxResolverSystem
