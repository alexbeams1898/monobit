#include "renderers/CombatDebugOverlay.h"

#include "ecs/BalanceConfig.h"
#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "ecs/GameComponents.h"
#include "utils/DebugDraw.h"

#include <cmath>

namespace CombatDebugOverlay
{

namespace
{
Mode sMode = Mode::Off;

constexpr Color kHurtbox{0.2f, 0.9f, 0.3f, 1.0f}; // green
constexpr Color kPushbox{0.95f, 0.85f, 0.2f, 1.0f}; // yellow
constexpr Color kHitbox{1.0f, 0.2f, 0.2f, 1.0f};  // red

// Draw a single collision shape. `tx, ty` is the entity's world Transform
// position; `scale` is the Transform.scale. For local-space shapes (hurtboxes
// relative to transform), pass the entity's scale so offsets and sizes grow
// with the character. For world-space shapes (already-transformed hitboxes),
// pass tx=ty=0 and scale=1.
void drawShape(const CollisionShape& s, float tx, float ty, float scale, const Color& color)
{
    switch (s.kind)
    {
    case ShapeKind::AABB:
        DebugDraw::rectOutline(tx + s.x * scale, ty + s.y * scale, s.w * 0.5f * scale,
                               s.h * 0.5f * scale, color);
        break;
    case ShapeKind::Circle:
        DebugDraw::circle(tx + s.x * scale, ty + s.y * scale, s.r * scale, color);
        break;
    case ShapeKind::Capsule:
        DebugDraw::capsule(tx + s.x * scale, ty + s.y * scale, tx + s.x2 * scale,
                           ty + s.y2 * scale, s.r * scale, color);
        break;
    }
}

// Transform a weapon-local shape into world space using the same math the
// HitboxResolverSystem uses so the overlay matches what actually tests.
CollisionShape transformShape(const CollisionShape& in, float ax, float ay, float facing_cos,
                              float facing_sin, float kf_x, float kf_y, float kf_cos,
                              float kf_sin)
{
    CollisionShape out = in;
    auto xf = [&](float& px, float& py)
    {
        const float rx = px * kf_cos - py * kf_sin;
        const float ry = px * kf_sin + py * kf_cos;
        float lx = rx + kf_x;
        float ly = ry + kf_y;
        const float wx = lx * facing_cos - ly * facing_sin;
        const float wy = lx * facing_sin + ly * facing_cos;
        px = wx + ax;
        py = wy + ay;
    };
    xf(out.x, out.y);
    if (out.kind == ShapeKind::Capsule)
        xf(out.x2, out.y2);
    return out;
}

void drawHurtboxes(EntityManager& em)
{
    auto& reg = em.registry();
    for (auto [e, t, hb] : reg.view<Transform, Hurtbox>().each())
    {
        (void)e;
        for (const auto& hs : hb.shapes)
            drawShape(hs.shape, t.x, t.y, t.scale, kHurtbox);
    }
}

void drawPushboxes(EntityManager& em)
{
    auto& reg = em.registry();
    for (auto [e, t, col] : reg.view<Transform, Collider>().each())
    {
        (void)e;
        if (!col.is_solid)
            continue;
        DebugDraw::rectOutline(t.x, t.y, col.width * 0.5f, col.height * 0.5f, kPushbox);
    }
}

void drawActiveHitboxes(EntityManager& em)
{
    auto& reg = em.registry();
    const auto* anim_data = reg.ctx().find<AttackAnimHitboxData>();
    if (anim_data == nullptr)
        return;

    for (auto [attacker, lock] : reg.view<AttackLocked>().each())
    {
        const Weapon* weapon =
            lock.left_hand ? reg.try_get<LeftWeapon>(attacker) : reg.try_get<Weapon>(attacker);
        if (weapon == nullptr || weapon->hitboxes.empty() || weapon->attack_anim.empty())
            continue;

        const auto* tf = reg.try_get<Transform>(attacker);
        const auto* anim = reg.try_get<Animation>(attacker);
        const auto* facing = reg.try_get<FacingDirection>(attacker);
        if (tf == nullptr || anim == nullptr || facing == nullptr)
            continue;

        const auto attackIt = anim_data->attacks.find(weapon->attack_anim);
        if (attackIt == anim_data->attacks.end())
            continue;

        const HitboxKeyframe* kf = nullptr;
        for (const auto& k : attackIt->second.keyframes)
        {
            if (k.frame == anim->frame_index)
            {
                kf = &k;
                break;
            }
        }
        if (kf == nullptr)
            continue;

        const float face_angle = std::atan2(facing->dy, facing->dx);
        const float fc = std::cos(face_angle);
        const float fs = std::sin(face_angle);
        const float kc = std::cos(kf->rotation);
        const float ks = std::sin(kf->rotation);

        for (const auto& whs : weapon->hitboxes)
        {
            const CollisionShape world =
                transformShape(whs.shape, tf->x, tf->y, fc, fs, kf->x, kf->y, kc, ks);
            drawShape(world, 0.0f, 0.0f, 1.0f, kHitbox);
        }
    }
}

} // namespace

void cycleMode()
{
    const int next = (static_cast<int>(sMode) + 1) % static_cast<int>(Mode::COUNT);
    sMode = static_cast<Mode>(next);
}

Mode mode()
{
    return sMode;
}

void render(EntityManager& em)
{
    if (sMode == Mode::Off)
        return;
    drawHurtboxes(em);
    if (sMode == Mode::HurtboxesPlusPushboxes || sMode == Mode::All)
        drawPushboxes(em);
    if (sMode == Mode::All)
        drawActiveHitboxes(em);
}

} // namespace CombatDebugOverlay
