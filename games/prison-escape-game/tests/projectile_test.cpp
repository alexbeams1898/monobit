#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "ecs/GameComponents.h"
#include "ecs/GameConfig.h"
#include "ops/InventoryOps.h"
#include "systems/ProjectileSystem.h"
#include "test_helpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

// ---------------------------------------------------------------------------
// Projectile system tests -- no window, no GPU, no SDL required.
//
// Projectiles have no Velocity component; ProjectileSystem moves them using
// dir_x/dir_y/speed stored on the Projectile component. Wall checks use the
// tile_map (not set up in unit tests, so wall tests are integration-only).
// ---------------------------------------------------------------------------

// Spawn a projectile with direction + speed (no Velocity).
// dirX/dirY should be normalized.
static entt::entity spawnProjectile(EntityManager& em, float x, float y, float dirX, float dirY,
                                    float speed, float maxRange, int pierce = 0)
{
    auto& reg = em.registry();
    const auto e = em.create();
    reg.emplace<Transform>(e, Transform{x, y, 0.0f, 1.0f});
    reg.emplace<Hitbox>(e, Hitbox{10.0f, entt::null});
    reg.emplace<Projectile>(e, Projectile{entt::null, maxRange, x, y, pierce, dirX, dirY, speed});
    return e;
}

// Helper: add stackable ammo items to an inventory.
static void addAmmo(Inventory& inv, const std::string& ammoType, int qty)
{
    ItemInstance item;
    item.config_path = ammoType;
    item.quantity = qty;
    inv.items.push_back(item);
}

static constexpr float TEST_DT = 1.0f / 60.0f;

// ---------------------------------------------------------------------------
// Range despawn
// ---------------------------------------------------------------------------

TEST_CASE("Projectile destroyed when beyond max range", "[projectile]")
{
    EntityManager em;
    emplaceGameConfigs(em);

    // Place projectile already beyond its max range.
    const auto e = spawnProjectile(em, 0.0f, 0.0f, 1.0f, 0.0f, 100.0f, 50.0f);
    auto& t = em.registry().get<Transform>(e);
    t.x = 60.0f; // beyond max_range of 50

    ProjectileSystem::update(em, TEST_DT);
    REQUIRE_FALSE(em.registry().valid(e));
}

TEST_CASE("Projectile survives within max range", "[projectile]")
{
    EntityManager em;
    emplaceGameConfigs(em);

    const auto e = spawnProjectile(em, 0.0f, 0.0f, 1.0f, 0.0f, 100.0f, 100.0f);
    // After one tick: 0 + 1*100*(1/60) ~ 1.67, well within range of 100.
    ProjectileSystem::update(em, TEST_DT);
    REQUIRE(em.registry().valid(e));
}

// ---------------------------------------------------------------------------
// Hit destroys on contact (pierce = 0)
// ---------------------------------------------------------------------------

TEST_CASE("Projectile destroyed on hit with pierce=0", "[projectile]")
{
    EntityManager em;
    emplaceGameConfigs(em);

    const auto e = spawnProjectile(em, 0.0f, 0.0f, 1.0f, 0.0f, 100.0f, 500.0f);
    em.registry().get<Hitbox>(e).hit_something = true;

    ProjectileSystem::update(em, TEST_DT);
    REQUIRE_FALSE(em.registry().valid(e));
}

// ---------------------------------------------------------------------------
// Pierce survives N hits
// ---------------------------------------------------------------------------

TEST_CASE("Projectile with pierce=2 survives 2 hits, dies on 3rd", "[projectile]")
{
    EntityManager em;
    emplaceGameConfigs(em);

    const auto e = spawnProjectile(em, 0.0f, 0.0f, 1.0f, 0.0f, 100.0f, 500.0f, 2);

    // First hit: survives, pierce decremented to 1.
    em.registry().get<Hitbox>(e).hit_something = true;
    ProjectileSystem::update(em, TEST_DT);
    REQUIRE(em.registry().valid(e));
    REQUIRE(em.registry().get<Projectile>(e).pierce_remaining == 1);
    REQUIRE_FALSE(em.registry().get<Hitbox>(e).hit_something);

    // Second hit: survives, pierce decremented to 0.
    em.registry().get<Hitbox>(e).hit_something = true;
    ProjectileSystem::update(em, TEST_DT);
    REQUIRE(em.registry().valid(e));
    REQUIRE(em.registry().get<Projectile>(e).pierce_remaining == 0);

    // Third hit: destroyed (pierce exhausted).
    em.registry().get<Hitbox>(e).hit_something = true;
    ProjectileSystem::update(em, TEST_DT);
    REQUIRE_FALSE(em.registry().valid(e));
}

// ---------------------------------------------------------------------------
// Movement: ProjectileSystem advances position each tick
// ---------------------------------------------------------------------------

TEST_CASE("ProjectileSystem moves projectile by dir * speed * dt", "[projectile]")
{
    EntityManager em;
    emplaceGameConfigs(em);

    const auto e = spawnProjectile(em, 100.0f, 200.0f, 1.0f, 0.0f, 600.0f, 9999.0f);
    ProjectileSystem::update(em, 0.1f); // 0.1s at 600 px/s = 60px

    const auto& t = em.registry().get<Transform>(e);
    REQUIRE(t.x == Catch::Approx(160.0f));
    REQUIRE(t.y == Catch::Approx(200.0f));
}

// ---------------------------------------------------------------------------
// Inventory-based reload fills magazine from ammo pool
// ---------------------------------------------------------------------------

TEST_CASE("Reload fills magazine from inventory ammo", "[projectile]")
{
    EntityManager em;
    emplaceGameConfigs(em);

    const std::string bulletType = "config/items/ammo/bullet.json";
    const auto e = em.create();

    auto& inv = em.registry().emplace<Inventory>(e);
    Equipment equip;
    addAmmo(inv, bulletType, 20);

    auto& rs = em.registry().emplace<RangedState>(e);
    rs.magazine_size = 8;
    rs.ammo_in_magazine = 0;

    const int reserve = InventoryOps::countItem(inv, bulletType);
    const int needed = rs.magazine_size - rs.ammo_in_magazine;
    const int fill = std::min(needed, reserve);
    InventoryOps::consumeItems(inv, equip, bulletType, fill);
    rs.ammo_in_magazine += fill;

    REQUIRE(rs.ammo_in_magazine == 8);
    REQUIRE(InventoryOps::countItem(inv, bulletType) == 12);
}

TEST_CASE("Reload with insufficient ammo fills partial magazine", "[projectile]")
{
    EntityManager em;
    emplaceGameConfigs(em);

    const std::string bulletType = "config/items/ammo/bullet.json";
    const auto e = em.create();

    auto& inv = em.registry().emplace<Inventory>(e);
    Equipment equip;
    addAmmo(inv, bulletType, 3);

    auto& rs = em.registry().emplace<RangedState>(e);
    rs.magazine_size = 8;
    rs.ammo_in_magazine = 0;

    const int reserve = InventoryOps::countItem(inv, bulletType);
    const int needed = rs.magazine_size - rs.ammo_in_magazine;
    const int fill = std::min(needed, reserve);
    InventoryOps::consumeItems(inv, equip, bulletType, fill);
    rs.ammo_in_magazine += fill;

    REQUIRE(rs.ammo_in_magazine == 3);
    REQUIRE(InventoryOps::countItem(inv, bulletType) == 0);
}

// ---------------------------------------------------------------------------
// Bow consumes arrows directly from inventory
// ---------------------------------------------------------------------------

TEST_CASE("Bow fire consumes arrow from inventory", "[projectile]")
{
    EntityManager em;
    emplaceGameConfigs(em);

    const std::string arrowType = "config/items/ammo/arrow.json";
    const auto e = em.create();

    auto& inv = em.registry().emplace<Inventory>(e);
    Equipment equip;
    addAmmo(inv, arrowType, 10);

    REQUIRE(InventoryOps::countItem(inv, arrowType) == 10);
    InventoryOps::consumeItems(inv, equip, arrowType, 1);
    REQUIRE(InventoryOps::countItem(inv, arrowType) == 9);
}

TEST_CASE("Cannot fire bow with no arrows", "[projectile]")
{
    EntityManager em;
    emplaceGameConfigs(em);

    const std::string arrowType = "config/items/ammo/arrow.json";
    const auto e = em.create();

    auto& inv = em.registry().emplace<Inventory>(e);
    REQUIRE(InventoryOps::countItem(inv, arrowType) == 0);
}

// ---------------------------------------------------------------------------
// RangedState lifecycle
// ---------------------------------------------------------------------------

TEST_CASE("RangedState emplaced for ranged weapon, removed for melee", "[projectile]")
{
    EntityManager em;
    emplaceGameConfigs(em);

    const auto e = em.create();

    auto& rs = em.registry().emplace<RangedState>(e);
    rs.magazine_size = 8;
    rs.ammo_in_magazine = 8;
    REQUIRE(em.registry().all_of<RangedState>(e));

    em.registry().remove<RangedState>(e);
    REQUIRE_FALSE(em.registry().all_of<RangedState>(e));
}

// ---------------------------------------------------------------------------
// CombatSystem skips projectiles in hitbox destroy loop
// ---------------------------------------------------------------------------

TEST_CASE("Projectile entities not destroyed by hitbox cleanup", "[projectile]")
{
    EntityManager em;
    emplaceGameConfigs(em);

    const auto proj = spawnProjectile(em, 0.0f, 0.0f, 1.0f, 0.0f, 100.0f, 500.0f);

    const auto hb = em.create();
    em.registry().emplace<Transform>(hb, Transform{0.0f, 0.0f, 0.0f, 1.0f});
    em.registry().emplace<Hitbox>(hb, Hitbox{5.0f, entt::null});

    std::vector<entt::entity> toDestroy;
    for (auto entity : em.registry().view<Hitbox>())
    {
        if (em.registry().all_of<Projectile>(entity))
            continue;
        toDestroy.push_back(entity);
    }
    for (auto e : toDestroy)
        em.destroy(e);

    REQUIRE(em.registry().valid(proj));
    REQUIRE_FALSE(em.registry().valid(hb));
}
