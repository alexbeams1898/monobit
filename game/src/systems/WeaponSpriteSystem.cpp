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

    // Spawn/destroy right-hand weapon entities when weapon_icon changes.
    for (auto [entity, weapon, appearance] :
         reg.view<Weapon, AppearanceState>().each())
    {
        if (weapon.weapon_icon != appearance.synced_visual_weapon)
        {
            appearance.synced_visual_weapon = weapon.weapon_icon;
            destroyWeaponEntity(reg, weapon);

            if (!weapon.weapon_icon.empty())
                spawnWeaponEntity(reg, entity, weapon, false);
        }
    }

    // Spawn/destroy left-hand weapon entities.
    for (auto [entity, weapon, appearance] :
         reg.view<LeftWeapon, AppearanceState>().each())
    {
        if (weapon.weapon_icon != appearance.synced_visual_weapon_left)
        {
            appearance.synced_visual_weapon_left = weapon.weapon_icon;
            destroyWeaponEntity(reg, weapon);

            if (!weapon.weapon_icon.empty())
                spawnWeaponEntity(reg, entity, weapon, true);
        }
    }

    // Clean up stale weapon_entity refs on Weapon/LeftWeapon components whose
    // entity was destroyed externally.
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

void WeaponSpriteSystem::syncVisuals(EntityManager& em)
{
    ZoneScopedN("WeaponSpriteSystem::syncVisuals");
    auto& reg = em.registry();
    const auto* anchorData = reg.ctx().find<HandAnchorData>();
    const float alpha = em.render_alpha;

    std::vector<entt::entity> orphans;

    for (auto [wpnEntity, wpnTag, wpnTransform, wpnSprite] :
         reg.view<WeaponSprite, Transform, Sprite>().each())
    {
        const auto wielder = wpnTag.wielder;
        if (!reg.valid(wielder) || !reg.all_of<Transform, Animation>(wielder))
        {
            orphans.push_back(wpnEntity);
            continue;
        }

        const auto& wielderPos = reg.get<Transform>(wielder);
        const auto& anim = reg.get<Animation>(wielder);
        const auto& wielderSprite = reg.get<Sprite>(wielder);

        // Read the column and frame directly from the wielder's already-rendered
        // sprite rect. This is the single source of truth -- whatever the
        // character is visibly showing is what the weapon keys off of. No
        // independent direction math, so no possibility of a one-frame drift.
        const int framesPerDir = anim.max_frames_per_state > 0 ? anim.max_frames_per_state : 1;
        const int frameWidth = anim.frame_width > 0 ? anim.frame_width : 1;
        const int absCol = wielderSprite.src_x / frameWidth;
        const int dirCol = absCol / framesPerDir;
        const int visibleFrame = absCol % framesPerDir;
        const int currentRow = wielderSprite.src_y / (anim.frame_height > 0 ? anim.frame_height : 1);

        // Look up the primary hand anchor for this weapon. Right-hand weapons
        // use row.right, left-hand weapons use row.left. The secondary hand
        // (used for two-handed rendering) is the opposite.
        const bool isLeftHand = wpnTag.left_hand;

        // During attack, hide the non-attacking hand's weapon.
        const auto* attackLock = reg.try_get<AttackLocked>(wielder);
        if (attackLock != nullptr && attackLock->left_hand != isLeftHand)
        {
            wpnSprite.src_w = 0;
            wpnSprite.src_h = 0;
            wpnTag.prev_row = -1;
            continue;
        }

        float anchorX = 0.0f;
        float anchorY = 0.0f;
        bool hasAnchor = false;
        float secondaryAnchorX = 0.0f;
        float secondaryAnchorY = 0.0f;
        bool hasSecondaryAnchor = false;

        if (anchorData != nullptr)
        {
            const auto rowIt = anchorData->rows.find(currentRow);
            if (rowIt != anchorData->rows.end())
            {
                const auto& row = rowIt->second;
                const auto& primaryAnchors = isLeftHand ? row.left : row.right;
                const auto& secondaryAnchors = isLeftHand ? row.right : row.left;

                if (dirCol >= 0 && dirCol < static_cast<int>(primaryAnchors.size()))
                {
                    const auto& frames = primaryAnchors[dirCol];
                    if (visibleFrame >= 0 && visibleFrame < static_cast<int>(frames.size()))
                    {
                        anchorX = frames[visibleFrame].x;
                        anchorY = frames[visibleFrame].y;
                        hasAnchor = true;
                    }
                }
                if (dirCol >= 0 && dirCol < static_cast<int>(secondaryAnchors.size()))
                {
                    const auto& frames = secondaryAnchors[dirCol];
                    if (visibleFrame >= 0 && visibleFrame < static_cast<int>(frames.size()))
                    {
                        secondaryAnchorX = frames[visibleFrame].x;
                        secondaryAnchorY = frames[visibleFrame].y;
                        hasSecondaryAnchor = true;
                    }
                }
            }
        }

        // No primary anchor for this row (e.g. death, hit) -- hide weapon.
        if (!hasAnchor)
        {
            wpnTag.prev_row = -1;
            wpnSprite.src_w = 0;
            wpnSprite.src_h = 0;
            continue;
        }

        // Smooth the anchor within the same animation row/direction to
        // prevent frame-to-frame jitter. Snap immediately on row or
        // direction changes (attack → run, turn, etc.) since the anchor
        // positions are from completely different body poses.
        static constexpr float ANCHOR_BLEND = 0.25f;
        const bool dirChanged = (dirCol != wpnTag.prev_dir);
        wpnTag.prev_row = currentRow;
        wpnTag.prev_dir = dirCol;

        if (dirChanged)
        {
            wpnTag.smooth_anchor_x = anchorX;
            wpnTag.smooth_anchor_y = anchorY;
        }
        else
        {
            wpnTag.smooth_anchor_x += (anchorX - wpnTag.smooth_anchor_x) * ANCHOR_BLEND;
            wpnTag.smooth_anchor_y += (anchorY - wpnTag.smooth_anchor_y) * ANCHOR_BLEND;
        }
        anchorX = wpnTag.smooth_anchor_x;
        anchorY = wpnTag.smooth_anchor_y;

        wpnSprite.src_w = WEAPON_ICON_SIZE;
        wpnSprite.src_h = WEAPON_ICON_SIZE;

        wpnTransform.scale = 1.0f;

        reg.get_or_emplace<PreviousTransform>(wpnEntity) = {wpnTransform.x, wpnTransform.y};

        // Read grip + two-hand state from the correct hand's weapon component.
        const Weapon* wielderWeapon = isLeftHand
            ? static_cast<const Weapon*>(reg.try_get<LeftWeapon>(wielder))
            : reg.try_get<Weapon>(wielder);

        // Hide this hand's weapon when the OTHER hand is in two-handed mode.
        const Weapon* otherWeapon = isLeftHand
            ? reg.try_get<Weapon>(wielder)
            : static_cast<const Weapon*>(reg.try_get<LeftWeapon>(wielder));
        if (otherWeapon != nullptr && otherWeapon->two_handed_active)
        {
            wpnSprite.src_w = 0;
            wpnSprite.src_h = 0;
            continue;
        }
        const float wpnScale = wielderWeapon ? wielderWeapon->weapon_scale : 1.0f;
        wpnTransform.scale = wpnScale;
        const float gripX = wielderWeapon ? wielderWeapon->grip_x : 0.0f;
        const float gripY = wielderWeapon ? wielderWeapon->grip_y : 0.0f;
        // Hand anchors are measured in source-pixel units against the 64x64
        // frame center. When the wielder is drawn at a non-1.0 scale, the
        // rendered body is `src_h * scale` pixels tall, so the anchor offset
        // in world units must also be multiplied by the wielder's scale to
        // reach the correct hand pixel. Grip offsets, however, stay in raw
        // pixel units: the weapon icon itself is always drawn at scale 1.0
        // (an AK is the same size regardless of who holds it), so the grip
        // pixel inside the icon is unscaled.
        const float wielderScale = wielderPos.scale;
        const auto* wielderCol = reg.try_get<Collider>(wielder);
        const float wielderYOffset =
            wielderCol
                ? (static_cast<float>(wielderSprite.src_h) * wielderScale - wielderCol->height) * 0.5f
                : 0.0f;

        const float halfIcon = WEAPON_ICON_SIZE * 0.5f;

        // Decide flip + rotation based on 1H direction table or 2H math.
        bool flip = false;
        float rot = 0.0f;

        const bool isTwoHanded = wielderWeapon != nullptr &&
                                  wielderWeapon->two_handed_active && hasSecondaryAnchor;

        if (isTwoHanded)
        {
            // 2H: compute rotation that aligns the grip→foregrip vector
            // with the primary→secondary hand vector. Use idle-frame
            // anchors (row 0) for stability — the rotation stays constant
            // across all animation states.
            const float iconDX = wielderWeapon->fore_grip_x - wielderWeapon->grip_x;
            const float iconDY = wielderWeapon->fore_grip_y - wielderWeapon->grip_y;

            // Read idle anchors for this direction.
            float idleHandDX = secondaryAnchorX - anchorX;
            float idleHandDY = secondaryAnchorY - anchorY;
            if (anchorData != nullptr)
            {
                const auto idleIt = anchorData->rows.find(0);
                if (idleIt != anchorData->rows.end())
                {
                    const auto& idleRow = idleIt->second;
                    const auto& pri = isLeftHand ? idleRow.left : idleRow.right;
                    const auto& sec = isLeftHand ? idleRow.right : idleRow.left;
                    if (dirCol < static_cast<int>(pri.size()) && !pri[dirCol].empty() &&
                        dirCol < static_cast<int>(sec.size()) && !sec[dirCol].empty())
                    {
                        idleHandDX = sec[dirCol][0].x - pri[dirCol][0].x;
                        idleHandDY = sec[dirCol][0].y - pri[dirCol][0].y;
                    }
                }
            }

            // Flip when secondary hand is to the left of primary.
            flip = (idleHandDX < 0.0f);
            const float thetaHand = std::atan2(idleHandDY, idleHandDX);
            const float thetaIcon = std::atan2(iconDY, flip ? -iconDX : iconDX);
            rot = thetaHand - thetaIcon;
        }
        else
        {
            // 1H: per-direction table. base_rotation shifts the icon's
            // resting angle. When flipped, negate it so it mirrors correctly.
            const float baseRot = wielderWeapon ? wielderWeapon->base_rotation : 0.0f;
            if (isLeftHand)
            {
                switch (dirCol)
                {
                    case 0: flip = false; rot = baseRot;  break;  // S
                    case 1: flip = true;  rot = -baseRot; break;  // W
                    case 2: flip = false; rot = baseRot;  break;  // E
                    case 3: flip = true;  rot = -baseRot; break;  // N
                    default: break;
                }
            }
            else
            {
                switch (dirCol)
                {
                    case 0: flip = true;  rot = -baseRot; break;  // S
                    case 1: flip = true;  rot = -baseRot; break;  // W
                    case 2: flip = false; rot = baseRot;  break;  // E
                    case 3: flip = false; rot = baseRot;  break;  // N
                    default: break;
                }
            }
        }

        wpnSprite.flip_x = flip;
        wpnSprite.rotation = rot;
        wpnSprite.geo_mirror_x = false;

        // Place the icon so the primary grip pixel lands on the left hand.
        // The grip offset is transformed by the same flip/mirror/rotation
        // that the GPU applies, so the math stays in sync. Multiply by
        // weapon_scale because the rendered icon is larger than the source.
        float gripOffX = (gripX - halfIcon) * wpnScale;
        float gripOffY = (gripY - halfIcon) * wpnScale;
        if (flip || wpnSprite.geo_mirror_x)
            gripOffX = -gripOffX;
        const float c = std::cos(rot);
        const float s = std::sin(rot);
        const float transformedOffX = c * gripOffX - s * gripOffY;
        const float transformedOffY = s * gripOffX + c * gripOffY;

        // Use the interpolated wielder position so the weapon tracks the
        // character's rendered position, not the tick position. Without this,
        // fast movement (running) causes the weapon to jiggle.
        float wielderDrawX = wielderPos.x;
        float wielderDrawY = wielderPos.y;
        if (const auto* prevT = reg.try_get<PreviousTransform>(wielder))
        {
            wielderDrawX = prevT->x + (wielderPos.x - prevT->x) * alpha;
            wielderDrawY = prevT->y + (wielderPos.y - prevT->y) * alpha;
        }
        wielderDrawX = std::round(wielderDrawX);
        wielderDrawY = std::round(wielderDrawY);

        const float scaledAnchorX = anchorX * wielderScale;
        const float scaledAnchorY = anchorY * wielderScale;
        wpnTransform.x = wielderDrawX + scaledAnchorX - transformedOffX;
        wpnTransform.y = wielderDrawY - wielderYOffset + scaledAnchorY - transformedOffY;
        wpnSprite.sort_anchor =
            wielderCol ? wielderDrawY + wielderCol->height * 0.5f : wielderDrawY;

        // Sub-layer: weapon in front or behind body.
        // In 2H mode, weapon crosses the torso — in front for S/W/E,
        // behind for N (facing away from viewer).
        // In 1H mode, use depth_per_dir (authored for left hand, right
        // hand swaps E/W).
        if (isTwoHanded)
        {
            wpnSprite.sub_layer = (dirCol == 3) ? -1 : 1;
        }
        else if (anchorData != nullptr && dirCol < static_cast<int>(anchorData->depth_per_dir.size()))
        {
            int depth = anchorData->depth_per_dir[dirCol];
            if (!isLeftHand && (dirCol == 1 || dirCol == 2))
                depth = -depth;
            wpnSprite.sub_layer = depth;
        }
        else
        {
            wpnSprite.sub_layer = 1;
        }

    }

    // Clean up orphaned weapon entities (wielder destroyed).
    for (const auto e : orphans)
    {
        if (reg.valid(e))
            reg.destroy(e);
    }
}
