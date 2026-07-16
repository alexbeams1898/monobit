#include "Interaction.h"
#include "WorldItems.h"
#include "ecs/Components.h"
#include "ecs/EntityManager.h"

#include <catch2/catch_test_macros.hpp>

// The world is rebuilt from the authored map every time a pilgrim sets out, so what a walk
// REMOVED from it has to be filtered out here -- otherwise a taken thing is back on the
// ground, to be taken again, forever.

namespace
{
// One item lying in the world, and one gather node.
std::vector<ldtk::PickupPlacement> twoPickups()
{
    ldtk::PickupPlacement stone;
    stone.placement_id = "p_stone";
    stone.kind = ldtk::PickupPlacement::Kind::Item;
    stone.target = "river_stone";
    stone.cx = 100.0f;
    stone.cy = 100.0f;

    ldtk::PickupPlacement patch;
    patch.placement_id = "p_patch";
    patch.kind = ldtk::PickupPlacement::Kind::Loot;
    patch.target = "hillside_herbs";
    patch.cx = 200.0f;
    patch.cy = 200.0f;

    return {stone, patch};
}

inventory::Registry itemsWithStone()
{
    inventory::Registry r;
    inventory::ItemDef def;
    def.id = "river_stone";
    def.name = "Worn River Stone";
    def.icon = "assets/sprites/items/river_stone.png";
    r.defs["river_stone"] = def;
    return r;
}

loot::Registry lootWithHerbs()
{
    loot::Registry r;
    loot::Table t;
    t.id = "hillside_herbs";
    r.tables["hillside_herbs"] = t;
    return r;
}

// How many takeable things are in the world.
int spawnedCount(EntityManager& em)
{
    int n = 0;
    for (auto [e, inter] : em.registry().view<interaction::Interactable>().each())
        if (inter.action != interaction::ActionKind::None)
            ++n;
    return n;
}

// Is a placement present in the world?
bool present(EntityManager& em, const std::string& placementId)
{
    for (auto [e, inter] : em.registry().view<interaction::Interactable>().each())
        if (inter.placement_id == placementId)
            return true;
    return false;
}
} // namespace

TEST_CASE("a fresh walk gets everything the map places", "[world_items]")
{
    EntityManager em;
    const world_items::Config cfg;
    world_items::spawn(em, twoPickups(), itemsWithStone(), lootWithHerbs(), cfg, /*gone=*/{});
    REQUIRE(spawnedCount(em) == 2);
}

TEST_CASE("what a pilgrim already took is not put back", "[world_items]")
{
    EntityManager em;
    const world_items::Config cfg;
    const std::unordered_set<std::string> gone = {"p_stone"};
    world_items::spawn(em, twoPickups(), itemsWithStone(), lootWithHerbs(), cfg, gone);

    REQUIRE(spawnedCount(em) == 1);
    REQUIRE_FALSE(present(em, "p_stone")); // taken on a previous visit -- stays taken
    REQUIRE(present(em, "p_patch"));       // untouched -- still there
}

TEST_CASE("a spawned pickup carries its identity, so taking it can be remembered", "[world_items]")
{
    // Without this the entity dies anonymously and the removal can't be recorded.
    EntityManager em;
    const world_items::Config cfg;
    world_items::spawn(em, twoPickups(), itemsWithStone(), lootWithHerbs(), cfg);

    REQUIRE(present(em, "p_stone"));
    REQUIRE(present(em, "p_patch"));
}

TEST_CASE("a walk that took everything gets an empty world", "[world_items]")
{
    EntityManager em;
    const world_items::Config cfg;
    const std::unordered_set<std::string> gone = {"p_stone", "p_patch"};
    world_items::spawn(em, twoPickups(), itemsWithStone(), lootWithHerbs(), cfg, gone);
    REQUIRE(spawnedCount(em) == 0);
}
