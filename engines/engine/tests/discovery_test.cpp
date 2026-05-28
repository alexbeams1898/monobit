#include "ecs/Items.h"

#include <catch2/catch_test_macros.hpp>

using engine::ecs::Compendium;

TEST_CASE("Compendium::discover returns true for new items", "[discovery]")
{
    Compendium comp;

    REQUIRE(comp.discover("config/items/weapons/shiv.json"));
    REQUIRE_FALSE(comp.discover("config/items/weapons/shiv.json")); // already discovered
}

TEST_CASE("Compendium::isDiscovered tracks discovered items", "[discovery]")
{
    Compendium comp;

    REQUIRE_FALSE(comp.isDiscovered("config/items/weapons/dagger.json"));
    comp.discover("config/items/weapons/dagger.json");
    REQUIRE(comp.isDiscovered("config/items/weapons/dagger.json"));
}

TEST_CASE("Compendium tracks multiple distinct items", "[discovery]")
{
    Compendium comp;

    comp.discover("item_a");
    comp.discover("item_b");
    comp.discover("item_c");

    REQUIRE(comp.isDiscovered("item_a"));
    REQUIRE(comp.isDiscovered("item_b"));
    REQUIRE(comp.isDiscovered("item_c"));
    REQUIRE_FALSE(comp.isDiscovered("item_d"));
}
