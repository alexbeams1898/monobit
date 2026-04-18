#include "ecs/BalanceConfig.h"
#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "ecs/GameComponents.h"
#include "systems/WeaponSpriteSystem.h"
#include "test_helpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

// Helper: set the wielder's sprite rect to a given direction column and frame.
// syncVisuals reads these fields directly -- they're normally written by
// AnimationSystem but in unit tests we set them by hand.
static void setWielderSpriteRect(EntityManager& em, entt::entity wielder, int dirCol, int frame,
                                 int row = 1)
{
    static constexpr int FRAME_WIDTH = 64;
    static constexpr int FRAME_HEIGHT = 64;
    static constexpr int FRAMES_PER_DIR = 13;
    auto& sprite = em.registry().get<Sprite>(wielder);
    sprite.src_x = (dirCol * FRAMES_PER_DIR + frame) * FRAME_WIDTH;
    sprite.src_y = row * FRAME_HEIGHT;
    sprite.src_w = FRAME_WIDTH;
    sprite.src_h = FRAME_HEIGHT;
}

// Helper: create a wielder entity with the minimum components for WeaponSpriteSystem.
static entt::entity createWielder(EntityManager& em, float x, float y)
{
    auto& reg = em.registry();
    const auto e = reg.create();
    reg.emplace<Transform>(e, Transform{x, y});
    reg.emplace<PreviousTransform>(e, PreviousTransform{x, y});

    Animation anim;
    anim.current_row = 1; // walk
    anim.direction_count = 4;
    anim.frame_index = 0;
    anim.frame_width = 64;
    anim.frame_height = 64;
    anim.max_frames_per_state = 13;
    reg.emplace<Animation>(e, anim);

    // Wielder needs a Sprite so syncVisuals can read src_x/src_y. Default =
    // South (col 0), walk row (1), frame 0.
    Sprite sprite;
    sprite.src_x = 0;
    sprite.src_y = 64; // row 1 * frame_height
    sprite.src_w = 64;
    sprite.src_h = 64;
    reg.emplace<Sprite>(e, sprite);

    Weapon w;
    w.visual_weapon = "test_sword";
    w.weapon_icon = "assets/sprites/test_weapon.png";
    // Grip at icon center: zero offset from the hand anchor. Tests that
    // validate anchor application don't have to reason about grip math.
    w.grip_x = 16.0f;
    w.grip_y = 16.0f;
    reg.emplace<Weapon>(e, w);

    AppearanceState app;
    // Leave synced_visual_weapon empty so the system detects a change.
    reg.emplace<AppearanceState>(e, app);

    reg.emplace<Collider>(e, Collider{32.0f, 48.0f, true});

    return e;
}

// Helper: install basic hand anchor data into ctx.
// Only left-hand anchors are installed -- two-handed rendering tests should
// supply right-hand data explicitly.
static void installAnchors(EntityManager& em)
{
    auto& anchors = em.registry().ctx().emplace<HandAnchorData>();
    anchors.depth_per_dir = {1, 1, 1, -1};

    // Walk row (1), 8 frames per direction.
    HandAnchorRow walkRow;
    walkRow.left.resize(4);
    walkRow.right.resize(4);
    for (int d = 0; d < 4; ++d)
    {
        for (int f = 0; f < 8; ++f)
        {
            const float xBase = (d == 0) ? 8.0f : (d == 1) ? -10.0f : (d == 2) ? 10.0f : -8.0f;
            walkRow.left[d].push_back({xBase + static_cast<float>(f) * 0.1f, 7.0f});
        }
    }
    anchors.rows[1] = walkRow;

    // Idle row (0), 1 frame per direction.
    HandAnchorRow idleRow;
    idleRow.left.resize(4);
    idleRow.right.resize(4);
    idleRow.left[0].push_back({8.0f, 8.0f});
    idleRow.left[1].push_back({-10.0f, 8.0f});
    idleRow.left[2].push_back({10.0f, 8.0f});
    idleRow.left[3].push_back({-8.0f, 6.0f});
    anchors.rows[0] = idleRow;
}

TEST_CASE("WeaponSpriteSystem: spawns weapon entity on equip", "[weapon_sprite]")
{
    EntityManager em;
    emplaceGameConfigs(em);
    installAnchors(em);

    const auto wielder = createWielder(em, 100.0f, 200.0f);

    WeaponSpriteSystem::updateEquipment(em);
    WeaponSpriteSystem::syncVisuals(em);

    const auto& w = em.registry().get<Weapon>(wielder);
    REQUIRE(em.registry().valid(w.weapon_entity));
    REQUIRE(em.registry().all_of<WeaponSprite>(w.weapon_entity));
    REQUIRE(em.registry().all_of<Sprite>(w.weapon_entity));
    REQUIRE(em.registry().all_of<Transform>(w.weapon_entity));

    const auto& tag = em.registry().get<WeaponSprite>(w.weapon_entity);
    REQUIRE(tag.wielder == wielder);
}

TEST_CASE("WeaponSpriteSystem: destroys weapon entity on unequip", "[weapon_sprite]")
{
    EntityManager em;
    emplaceGameConfigs(em);
    installAnchors(em);

    const auto wielder = createWielder(em, 100.0f, 200.0f);

    // First update spawns the weapon.
    WeaponSpriteSystem::updateEquipment(em);
    WeaponSpriteSystem::syncVisuals(em);
    const auto wpnEntity = em.registry().get<Weapon>(wielder).weapon_entity;
    REQUIRE(em.registry().valid(wpnEntity));

    // Unequip.
    auto& w = em.registry().get<Weapon>(wielder);
    w.visual_weapon.clear();
    w.weapon_icon.clear();

    WeaponSpriteSystem::updateEquipment(em);
    WeaponSpriteSystem::syncVisuals(em);

    REQUIRE_FALSE(em.registry().valid(w.weapon_entity));
    REQUIRE_FALSE(em.registry().valid(wpnEntity));
}

TEST_CASE("WeaponSpriteSystem: destroys weapon when wielder destroyed", "[weapon_sprite]")
{
    EntityManager em;
    emplaceGameConfigs(em);
    installAnchors(em);

    const auto wielder = createWielder(em, 100.0f, 200.0f);

    WeaponSpriteSystem::updateEquipment(em);
    WeaponSpriteSystem::syncVisuals(em);
    const auto wpnEntity = em.registry().get<Weapon>(wielder).weapon_entity;
    REQUIRE(em.registry().valid(wpnEntity));

    // Destroy wielder.
    em.registry().destroy(wielder);

    // Next update should clean up orphan.
    WeaponSpriteSystem::updateEquipment(em);
    WeaponSpriteSystem::syncVisuals(em);
    REQUIRE_FALSE(em.registry().valid(wpnEntity));
}

TEST_CASE("WeaponSpriteSystem: sub_layer matches depth_per_dir", "[weapon_sprite]")
{
    EntityManager em;
    emplaceGameConfigs(em);
    installAnchors(em);

    const auto wielder = createWielder(em, 100.0f, 200.0f);

    // Default rect: South (column 0) -> depth_per_dir[0] = +1.
    WeaponSpriteSystem::updateEquipment(em);
    WeaponSpriteSystem::syncVisuals(em);
    const auto wpnEntity = em.registry().get<Weapon>(wielder).weapon_entity;
    REQUIRE(em.registry().get<Sprite>(wpnEntity).sub_layer == 1);

    // Change the wielder's rendered rect to North (column 3) -> depth = -1.
    setWielderSpriteRect(em, wielder, /*dirCol=*/3, /*frame=*/0, /*row=*/1);
    WeaponSpriteSystem::syncVisuals(em);
    REQUIRE(em.registry().get<Sprite>(wpnEntity).sub_layer == -1);
}

TEST_CASE("WeaponSpriteSystem: anchor offset applied to position", "[weapon_sprite]")
{
    EntityManager em;
    emplaceGameConfigs(em);
    installAnchors(em);

    const auto wielder = createWielder(em, 100.0f, 200.0f);
    // Use East (col 2) so flip=false and the grip math is the identity.
    // With grip=(16,16), the icon center lands exactly on the hand anchor.
    setWielderSpriteRect(em, wielder, /*dirCol=*/2, /*frame=*/0, /*row=*/1);

    WeaponSpriteSystem::updateEquipment(em);
    WeaponSpriteSystem::syncVisuals(em);
    const auto wpnEntity = em.registry().get<Weapon>(wielder).weapon_entity;
    const auto& t = em.registry().get<Transform>(wpnEntity);

    // East walk frame 0 anchor = (10.0, 7.0) from installAnchors.
    // wielderYOffset = (sprite.src_h - collider.height) / 2 = (64 - 48) / 2 = 8.
    // wpnTransform.x = wielderPos.x + anchorX = 100 + 10 = 110.
    // wpnTransform.y = wielderPos.y - yOffset + anchorY = 200 - 8 + 7 = 199.
    REQUIRE(t.x == Catch::Approx(110.0f));
    REQUIRE(t.y == Catch::Approx(199.0f));
}

TEST_CASE("WeaponSpriteSystem: sort_anchor matches wielder sort_y", "[weapon_sprite]")
{
    EntityManager em;
    emplaceGameConfigs(em);
    installAnchors(em);

    const auto wielder = createWielder(em, 100.0f, 200.0f);

    WeaponSpriteSystem::updateEquipment(em);
    WeaponSpriteSystem::syncVisuals(em);
    const auto wpnEntity = em.registry().get<Weapon>(wielder).weapon_entity;
    const auto& sprite = em.registry().get<Sprite>(wpnEntity);

    // Wielder sort_y = y + collider.height * 0.5 = 200 + 24 = 224.
    REQUIRE(sprite.use_sort_anchor);
    REQUIRE(sprite.sort_anchor == Catch::Approx(224.0f));
}

TEST_CASE("WeaponSpriteSystem: no crash without HandAnchorData", "[weapon_sprite]")
{
    EntityManager em;
    emplaceGameConfigs(em);
    // Deliberately no installAnchors().

    const auto wielder = createWielder(em, 100.0f, 200.0f);

    // Should not crash.
    WeaponSpriteSystem::updateEquipment(em);
    WeaponSpriteSystem::syncVisuals(em);

    const auto wpnEntity = em.registry().get<Weapon>(wielder).weapon_entity;
    REQUIRE(em.registry().valid(wpnEntity));

    // Weapon should be hidden (no anchors -> src_w=0).
    const auto& sprite = em.registry().get<Sprite>(wpnEntity);
    REQUIRE(sprite.src_w == 0);
}

TEST_CASE("WeaponSpriteSystem: no weapon icon -> no weapon entity", "[weapon_sprite]")
{
    EntityManager em;
    emplaceGameConfigs(em);
    installAnchors(em);

    auto& reg = em.registry();
    const auto e = reg.create();
    reg.emplace<Transform>(e, Transform{100.0f, 200.0f});
    reg.emplace<PreviousTransform>(e, PreviousTransform{100.0f, 200.0f});
    reg.emplace<Animation>(e);

    Weapon w;
    w.visual_weapon = "fists";
    // No weapon_icon -> empty.
    reg.emplace<Weapon>(e, w);
    reg.emplace<AppearanceState>(e);

    WeaponSpriteSystem::updateEquipment(em);
    WeaponSpriteSystem::syncVisuals(em);

    REQUIRE_FALSE(reg.valid(reg.get<Weapon>(e).weapon_entity));
}
