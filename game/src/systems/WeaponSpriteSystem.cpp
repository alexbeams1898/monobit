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

static void spawnWeaponEntity(entt::registry& reg, entt::entity wielder, Weapon& weapon)
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
    reg.emplace<WeaponSprite>(wpnEntity, WeaponSprite{wielder});

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

    // Spawn/destroy weapon entities when the wielder's visual_weapon changes.
    for (auto [entity, weapon, appearance] :
         reg.view<Weapon, AppearanceState>().each())
    {
        if (weapon.visual_weapon != appearance.synced_visual_weapon)
        {
            appearance.synced_visual_weapon = weapon.visual_weapon;
            destroyWeaponEntity(reg, weapon);

            if (!weapon.weapon_icon.empty())
                spawnWeaponEntity(reg, entity, weapon);
        }
    }

    // Clean up stale weapon_entity refs on Weapon components whose entity
    // was destroyed externally.
    for (auto [entity, weapon] : reg.view<Weapon>().each())
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

        // Look up LEFT-hand anchor for the current row/dir/frame. Left is
        // the primary grip for one-handed weapons and the trigger hand for
        // two-handed weapons.
        float anchorX = 0.0f;
        float anchorY = 0.0f;
        bool hasAnchor = false;

        // Also look up the right-hand anchor (optional, used for two-hand
        // rendering). If the row has no right-hand data or it's empty for
        // this direction/frame, we silently fall back to one-hand rendering
        // even if the weapon is two_handed_active.
        float rightAnchorX = 0.0f;
        float rightAnchorY = 0.0f;
        bool hasRightAnchor = false;

        if (anchorData != nullptr)
        {
            const auto rowIt = anchorData->rows.find(currentRow);
            if (rowIt != anchorData->rows.end())
            {
                const auto& row = rowIt->second;
                if (dirCol >= 0 && dirCol < static_cast<int>(row.left.size()))
                {
                    const auto& frames = row.left[dirCol];
                    if (visibleFrame >= 0 && visibleFrame < static_cast<int>(frames.size()))
                    {
                        anchorX = frames[visibleFrame].x;
                        anchorY = frames[visibleFrame].y;
                        hasAnchor = true;
                    }
                }
                if (dirCol >= 0 && dirCol < static_cast<int>(row.right.size()))
                {
                    const auto& frames = row.right[dirCol];
                    if (visibleFrame >= 0 && visibleFrame < static_cast<int>(frames.size()))
                    {
                        rightAnchorX = frames[visibleFrame].x;
                        rightAnchorY = frames[visibleFrame].y;
                        hasRightAnchor = true;
                    }
                }
            }
        }

        // No left (primary) anchor for this row (e.g. death, hit) -- hide weapon.
        if (!hasAnchor)
        {
            wpnSprite.src_w = 0;
            wpnSprite.src_h = 0;
            continue;
        }

        wpnSprite.src_w = WEAPON_ICON_SIZE;
        wpnSprite.src_h = WEAPON_ICON_SIZE;

        // Weapon sprite stays at its native 1.0 scale regardless of the
        // wielder's scale -- an AK looks the same size whether a small or
        // large character holds it.
        wpnTransform.scale = 1.0f;

        // Snapshot previous position for render interpolation.
        reg.get_or_emplace<PreviousTransform>(wpnEntity) = {wpnTransform.x, wpnTransform.y};

        // Read grip + two-hand state from wielder's Weapon component.
        const auto* wielderWeapon = reg.try_get<Weapon>(wielder);
        const float wpnScale = wielderWeapon ? wielderWeapon->weapon_scale : 1.0f;
        wpnTransform.scale = wpnScale;
        const float gripX = wielderWeapon ? wielderWeapon->grip_x : 0.0f;
        const float gripY = wielderWeapon ? wielderWeapon->grip_y : 0.0f;
        const float foreGripX = wielderWeapon ? wielderWeapon->fore_grip_x : 0.0f;
        const float foreGripY = wielderWeapon ? wielderWeapon->fore_grip_y : 0.0f;
        const bool wantTwoHanded =
            wielderWeapon != nullptr && wielderWeapon->two_handed &&
            wielderWeapon->two_handed_active;

        // Two-hand rendering requires BOTH a right-hand anchor for this frame
        // AND a fore-grip pixel on the weapon. If either is missing, fall back
        // to one-hand rendering for this frame (silent degradation).
        const bool foreGripDistinct = wielderWeapon != nullptr &&
                                       (wielderWeapon->fore_grip_x != wielderWeapon->grip_x ||
                                        wielderWeapon->fore_grip_y != wielderWeapon->grip_y);
        const bool renderTwoHanded = wantTwoHanded && hasRightAnchor && foreGripDistinct;

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

        if (renderTwoHanded)
        {
            // TWO-HANDED: compute rotation that aligns the grip-to-foregrip
            // vector in icon space with the left-hand-to-right-hand vector
            // in world space. No flip_x; rotation drives all orientation.
            const float handLX = wielderPos.x + anchorX * wielderScale;
            const float handLY = wielderPos.y - wielderYOffset + anchorY * wielderScale;
            const float handRX = wielderPos.x + rightAnchorX * wielderScale;
            const float handRY = wielderPos.y - wielderYOffset + rightAnchorY * wielderScale;
            // For S/W/E: left-handed hold with geo_mirror_x. The mirror
            // negates the grip vector's x so the rotation aligns correctly.
            // For N: the character faces away, so left/right are visually
            // swapped. No mirror needed — the un-mirrored rotation already
            // puts the gun in the correct orientation from behind.
            const bool northFacing = (dirCol == 3);
            const float rawGripDX = foreGripX - gripX;
            const float gripDX = northFacing ? rawGripDX : -rawGripDX;
            const float gripDY = foreGripY - gripY;
            const float handDX = handRX - handLX;
            const float handDY = handRY - handLY;
            const float gripAngle = std::atan2(gripDY, gripDX);

            // When the hands are very close together (mid-swing walk frames),
            // the hand vector is near-zero and atan2 produces erratic angles.
            // Hold the previous rotation until the hands separate again.
            static constexpr float MIN_HAND_DIST_SQ = 8.0f * 8.0f;
            static float s_lastGoodRot = 0.0f;
            const float handDistSq = handDX * handDX + handDY * handDY;
            if (handDistSq >= MIN_HAND_DIST_SQ)
            {
                const float handAngle = std::atan2(handDY, handDX);
                s_lastGoodRot = handAngle - gripAngle;
            }

            flip = false;
            rot = s_lastGoodRot;
            wpnSprite.geo_mirror_x = !northFacing;
        }
        else
        {
            // ONE-HANDED: per-direction table. Base icon points NE.
            //   base   -> NE    flip_x -> NW
            switch (dirCol)
            {
                case 0: flip = false; rot = 0.0f; break;   // S -> NE
                case 1: flip = true;  rot = 0.0f; break;   // W -> NW
                case 2: flip = false; rot = 0.0f; break;   // E -> NE
                case 3: flip = true;  rot = 0.0f; break;   // N -> NW
                default: break;
            }
        }

        wpnSprite.flip_x = flip;
        wpnSprite.rotation = rot;
        if (!renderTwoHanded)
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

        const float scaledAnchorX = anchorX * wielderScale;
        const float scaledAnchorY = anchorY * wielderScale;
        wpnTransform.x = wielderPos.x + scaledAnchorX - transformedOffX;
        wpnTransform.y = wielderPos.y - wielderYOffset + scaledAnchorY - transformedOffY;

        // Sort anchor MUST exactly equal the wielder's rendered sort_y so the
        // weapon always lands in the same depth bucket as its wielder. Match
        // RenderSystem: interpolate, then round (see RenderSystem.cpp around
        // drawY = round(prev + (curr - prev) * alpha)).
        float wielderDrawY = wielderPos.y;
        if (const auto* prevT = reg.try_get<PreviousTransform>(wielder))
            wielderDrawY = prevT->y + (wielderPos.y - prevT->y) * alpha;
        wielderDrawY = std::round(wielderDrawY);
        wpnSprite.sort_anchor =
            wielderCol ? wielderDrawY + wielderCol->height * 0.5f : wielderDrawY;

        // Sub-layer from depth_per_dir: weapon in front or behind body.
        // In 2H mode the gun crosses the torso — in front for most
        // directions, but behind when facing North (away from viewer).
        if (renderTwoHanded)
            wpnSprite.sub_layer = (dirCol == 3) ? -1 : 1;
        else if (anchorData != nullptr && dirCol < static_cast<int>(anchorData->depth_per_dir.size()))
            wpnSprite.sub_layer = anchorData->depth_per_dir[dirCol];
        else
            wpnSprite.sub_layer = 1;

    }

    // Clean up orphaned weapon entities (wielder destroyed).
    for (const auto e : orphans)
    {
        if (reg.valid(e))
            reg.destroy(e);
    }
}
