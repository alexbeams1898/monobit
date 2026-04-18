#include "systems/WeaponSpriteSystem.h"

#include "ecs/BalanceConfig.h"
#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "ecs/GameComponents.h"

#include <cmath>
#include <tracy/Tracy.hpp>
#include <vector>

// Weapon icon size in pixels (small inventory-style icon).
static constexpr int WEAPON_ICON_SIZE = 32;
static constexpr float kPi = 3.14159265358979323846f;

static void spawnWeaponEntity(entt::registry& reg, entt::entity wielder, Weapon& weapon,
                              bool left_hand = false)
{
    const auto wpnEntity = reg.create();
    const auto& wt = reg.get<Transform>(wielder);

    Sprite sprite;
    sprite.texture_path = weapon.weapon_icon;
    sprite.src_w = WEAPON_ICON_SIZE;
    sprite.src_h = WEAPON_ICON_SIZE;
    sprite.layer = reg.get<Sprite>(wielder).layer;
    sprite.use_sort_anchor = true;

    reg.emplace<Transform>(wpnEntity, Transform{wt.x, wt.y});
    reg.emplace<PreviousTransform>(wpnEntity, PreviousTransform{wt.x, wt.y});
    reg.emplace<Sprite>(wpnEntity, sprite);
    reg.emplace<WeaponSprite>(wpnEntity, WeaponSprite{wielder, left_hand});

    weapon.weapon_entity = wpnEntity;
}

static void destroyWeaponEntity(entt::registry& reg, Weapon& weapon)
{
    if (weapon.weapon_entity != entt::null && reg.valid(weapon.weapon_entity))
        reg.destroy(weapon.weapon_entity);
    weapon.weapon_entity = entt::null;
}

void WeaponSpriteSystem::updateEquipment(EntityManager& em)
{
    ZoneScopedN("WeaponSpriteSystem::updateEquipment");
    auto& reg = em.registry();

    for (auto [entity, weapon, appearance] : reg.view<Weapon, AppearanceState>().each())
    {
        if (weapon.weapon_icon != appearance.synced_visual_weapon)
        {
            appearance.synced_visual_weapon = weapon.weapon_icon;
            destroyWeaponEntity(reg, weapon);
            if (!weapon.weapon_icon.empty())
                spawnWeaponEntity(reg, entity, weapon, false);
        }
    }

    for (auto [entity, weapon, appearance] : reg.view<LeftWeapon, AppearanceState>().each())
    {
        if (weapon.weapon_icon != appearance.synced_visual_weapon_left)
        {
            appearance.synced_visual_weapon_left = weapon.weapon_icon;
            destroyWeaponEntity(reg, weapon);
            if (!weapon.weapon_icon.empty())
                spawnWeaponEntity(reg, entity, weapon, true);
        }
    }

    for (auto [entity, weapon] : reg.view<Weapon>().each())
    {
        if (weapon.weapon_entity != entt::null && !reg.valid(weapon.weapon_entity))
            weapon.weapon_entity = entt::null;
    }
    for (auto [entity, weapon] : reg.view<LeftWeapon>().each())
    {
        if (weapon.weapon_entity != entt::null && !reg.valid(weapon.weapon_entity))
            weapon.weapon_entity = entt::null;
    }
}

struct AnchorResult
{
    float x = 0.0f;
    float y = 0.0f;
    float rotation = -999.0f;
    int flip = -1;
    int depth = 0;
    bool found = false;
    float secondary_x = 0.0f;
    float secondary_y = 0.0f;
    bool has_secondary = false;
};

static AnchorResult lookupAnchors(const HandAnchorData* data, int row, int dir, int frame,
                                  bool leftHand)
{
    AnchorResult r;
    if (data == nullptr)
        return r;
    const auto it = data->rows.find(row);
    if (it == data->rows.end())
        return r;

    const auto& rd = it->second;
    const auto& pri = leftHand ? rd.left : rd.right;
    const auto& sec = leftHand ? rd.right : rd.left;

    if (dir >= 0 && dir < static_cast<int>(pri.size()) && frame >= 0 &&
        frame < static_cast<int>(pri[dir].size()))
    {
        const auto& a = pri[dir][frame];
        r.x = a.x;
        r.y = a.y;
        r.rotation = a.rotation;
        r.flip = a.flip;
        r.depth = a.depth;
        r.found = true;
    }
    if (dir >= 0 && dir < static_cast<int>(sec.size()) && frame >= 0 &&
        frame < static_cast<int>(sec[dir].size()))
    {
        r.secondary_x = sec[dir][frame].x;
        r.secondary_y = sec[dir][frame].y;
        r.has_secondary = true;
    }
    return r;
}

static void getIdleHandVector(const HandAnchorData* data, int dir, bool leftHand, float& dx,
                              float& dy)
{
    if (data == nullptr)
        return;
    const auto it = data->rows.find(0);
    if (it == data->rows.end())
        return;
    const auto& rd = it->second;
    const auto& pri = leftHand ? rd.left : rd.right;
    const auto& sec = leftHand ? rd.right : rd.left;
    if (dir < static_cast<int>(pri.size()) && !pri[dir].empty() &&
        dir < static_cast<int>(sec.size()) && !sec[dir].empty())
    {
        dx = sec[dir][0].x - pri[dir][0].x;
        dy = sec[dir][0].y - pri[dir][0].y;
    }
}

static void computeFlipRotation(const AnchorResult& a, const Weapon* w, const HandAnchorData* data,
                                int dir, bool leftHand, bool useNS, bool twoH, float gripX,
                                float gripY, bool& flip, float& rot)
{
    if (a.flip >= 0 || a.rotation > -900.0f)
    {
        const float base = w ? w->base_rotation : 0.0f;
        flip = (a.flip >= 0) ? (a.flip != 0) : false;
        rot = (a.rotation > -900.0f) ? a.rotation : (flip ? -base : base);
        return;
    }
    if (useNS)
    {
        flip = false;
        rot = (dir == 3) ? kPi : 0.0f;
        return;
    }
    if (twoH)
    {
        const float iconDX = w->fore_grip_x - gripX;
        const float iconDY = w->fore_grip_y - gripY;
        float handDX = a.secondary_x - a.x;
        float handDY = a.secondary_y - a.y;
        getIdleHandVector(data, dir, leftHand, handDX, handDY);
        flip = (handDX < 0.0f);
        rot = std::atan2(handDY, handDX) - std::atan2(iconDY, flip ? -iconDX : iconDX);
        return;
    }
    const float base = w ? w->base_rotation : 0.0f;
    flip = leftHand ? (dir == 1 || dir == 3) : (dir <= 1);
    rot = flip ? -base : base;
}

static int computeSubLayer(const AnchorResult& a, const HandAnchorData* data, int dir,
                           bool leftHand, bool twoH)
{
    if (a.depth != 0)
        return a.depth;
    if (twoH)
        return (dir == 3) ? -1 : 1;
    if (data != nullptr && dir < static_cast<int>(data->depth_per_dir.size()))
    {
        int d = data->depth_per_dir[dir];
        if (!leftHand && (dir == 1 || dir == 2))
            d = -d;
        return d;
    }
    return 1;
}

static void smoothAnchor(WeaponSprite& tag, AnchorResult& a, int dir)
{
    static constexpr float BLEND = 0.25f;
    const bool dirChanged = (dir != tag.prev_dir);
    tag.prev_dir = dir;
    if (dirChanged)
    {
        tag.smooth_anchor_x = a.x;
        tag.smooth_anchor_y = a.y;
    }
    else
    {
        tag.smooth_anchor_x += (a.x - tag.smooth_anchor_x) * BLEND;
        tag.smooth_anchor_y += (a.y - tag.smooth_anchor_y) * BLEND;
    }
    a.x = tag.smooth_anchor_x;
    a.y = tag.smooth_anchor_y;
}

static void hideWeapon(Sprite& s, WeaponSprite& t)
{
    s.src_w = 0;
    s.src_h = 0;
    t.prev_row = -1;
}

// Position one weapon sprite relative to its wielder. Returns false if hidden.
static bool syncOneWeapon(entt::registry& reg, const HandAnchorData* anchorData, float alpha,
                          entt::entity wpnEntity, WeaponSprite& tag, Transform& wpnT, Sprite& wpnS)
{
    const auto wielder = tag.wielder;
    const auto& wPos = reg.get<Transform>(wielder);
    const auto& anim = reg.get<Animation>(wielder);
    const auto& wSprite = reg.get<Sprite>(wielder);
    const bool leftHand = tag.left_hand;

    const int fpd = anim.max_frames_per_state > 0 ? anim.max_frames_per_state : 1;
    const int fw = anim.frame_width > 0 ? anim.frame_width : 1;
    const int absCol = wSprite.src_x / fw;
    const int dir = absCol / fpd;
    const int frame = absCol % fpd;
    const int row = wSprite.src_y / (anim.frame_height > 0 ? anim.frame_height : 1);

    const auto* atk = reg.try_get<AttackLocked>(wielder);
    if (atk != nullptr && atk->left_hand != leftHand)
    {
        hideWeapon(wpnS, tag);
        return false;
    }

    auto a = lookupAnchors(anchorData, row, dir, frame, leftHand);
    if (!a.found)
    {
        hideWeapon(wpnS, tag);
        return false;
    }

    tag.prev_row = row;
    smoothAnchor(tag, a, dir);

    wpnS.src_w = WEAPON_ICON_SIZE;
    wpnS.src_h = WEAPON_ICON_SIZE;
    reg.get_or_emplace<PreviousTransform>(wpnEntity) = {wpnT.x, wpnT.y};

    const Weapon* wep = leftHand ? static_cast<const Weapon*>(reg.try_get<LeftWeapon>(wielder))
                                 : reg.try_get<Weapon>(wielder);
    const Weapon* other = leftHand ? reg.try_get<Weapon>(wielder)
                                   : static_cast<const Weapon*>(reg.try_get<LeftWeapon>(wielder));
    if (other != nullptr && other->two_handed_active)
    {
        wpnS.src_w = 0;
        wpnS.src_h = 0;
        return false;
    }

    const bool useNS = wep != nullptr && !wep->attack_icon_ns.empty() && (dir == 0 || dir == 3);
    wpnS.texture_path = (useNS)            ? wep->attack_icon_ns
                        : (wep != nullptr) ? wep->weapon_icon
                                           : wpnS.texture_path;

    const float scale = wep ? wep->weapon_scale : 1.0f;
    wpnT.scale = scale;
    const float gx = (useNS && wep->attack_grip_ns_x != 0.0f) ? wep->attack_grip_ns_x
                     : (wep != nullptr)                       ? wep->grip_x
                                                              : 0.0f;
    const float gy = (useNS && wep->attack_grip_ns_y != 0.0f) ? wep->attack_grip_ns_y
                     : (wep != nullptr)                       ? wep->grip_y
                                                              : 0.0f;

    const float wScale = wPos.scale;
    const auto* wCol = reg.try_get<Collider>(wielder);
    const float yOff =
        wCol ? (static_cast<float>(wSprite.src_h) * wScale - wCol->height) * 0.5f : 0.0f;
    const float half = WEAPON_ICON_SIZE * 0.5f;
    const bool twoH = wep != nullptr && wep->two_handed_active && a.has_secondary;

    bool flip = false;
    float rot = 0.0f;
    computeFlipRotation(a, wep, anchorData, dir, leftHand, useNS, twoH, gx, gy, flip, rot);

    wpnS.flip_x = flip;
    wpnS.rotation = rot;
    wpnS.geo_mirror_x = false;

    const float goX = (gx - half) * scale * (flip ? -1.0f : 1.0f);
    const float goY = (gy - half) * scale;
    const float c = std::cos(rot);
    const float s = std::sin(rot);

    float dx = wPos.x;
    float dy = wPos.y;
    if (const auto* prev = reg.try_get<PreviousTransform>(wielder))
    {
        dx = prev->x + (wPos.x - prev->x) * alpha;
        dy = prev->y + (wPos.y - prev->y) * alpha;
    }
    dx = std::round(dx);
    dy = std::round(dy);

    wpnT.x = dx + a.x * wScale - (c * goX - s * goY);
    wpnT.y = dy - yOff + a.y * wScale - (s * goX + c * goY);
    wpnS.sort_anchor = wCol ? dy + wCol->height * 0.5f : dy;
    wpnS.sub_layer = computeSubLayer(a, anchorData, dir, leftHand, twoH);
    return true;
}

void WeaponSpriteSystem::syncVisuals(EntityManager& em)
{
    ZoneScopedN("WeaponSpriteSystem::syncVisuals");
    auto& reg = em.registry();
    const auto* anchorData = reg.ctx().find<HandAnchorData>();
    const float alpha = em.render_alpha;

    std::vector<entt::entity> orphans;
    for (auto [wpnEntity, tag, wpnT, wpnS] : reg.view<WeaponSprite, Transform, Sprite>().each())
    {
        if (!reg.valid(tag.wielder) || !reg.all_of<Transform, Animation>(tag.wielder))
        {
            orphans.push_back(wpnEntity);
            continue;
        }
        syncOneWeapon(reg, anchorData, alpha, wpnEntity, tag, wpnT, wpnS);
    }

    for (const auto e : orphans)
    {
        if (reg.valid(e))
            reg.destroy(e);
    }
}
