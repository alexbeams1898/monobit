#include "Inventory.h"

#include <cstdio>
#include <filesystem>
#include <fstream>

#include <catch2/catch_test_macros.hpp>

using namespace inventory;

namespace
{
// A registry with one stackable (cap 5) and one unique item, built in code so the
// op tests don't depend on config files.
Registry makeRegistry()
{
    Registry r;
    ItemDef herb;
    herb.id = "herb";
    herb.category = Category::Practical;
    herb.stackable = true;
    herb.max_stack = 5;
    r.defs["herb"] = herb;

    ItemDef key;
    key.id = "notebook";
    key.category = Category::KeyItem;
    key.stackable = false;
    key.max_stack = 1;
    r.defs["notebook"] = key;
    return r;
}
} // namespace

TEST_CASE("has/count on an empty satchel are false/zero", "[inventory]")
{
    Satchel s;
    REQUIRE_FALSE(has(s, "notebook"));
    REQUIRE(count(s, "notebook") == 0);
}

TEST_CASE("A unique key item adds and is found by has()", "[inventory]")
{
    const Registry r = makeRegistry();
    Satchel s;
    add(s, r, ItemInstance{"notebook"});
    REQUIRE(has(s, "notebook"));
    REQUIRE(count(s, "notebook") == 1);
    REQUIRE(s.items.size() == 1);
}

TEST_CASE("Stackables merge up to max_stack, then spill into new stacks", "[inventory]")
{
    const Registry r = makeRegistry(); // herb cap = 5
    Satchel s;
    add(s, r, ItemInstance{"herb", 3});
    REQUIRE(count(s, "herb") == 3);
    REQUIRE(s.items.size() == 1);

    // 3 + 4 = 7 -> one full stack of 5 + a new stack of 2.
    add(s, r, ItemInstance{"herb", 4});
    REQUIRE(count(s, "herb") == 7);
    REQUIRE(s.items.size() == 2);
    REQUIRE(s.items[0].quantity == 5);
    REQUIRE(s.items[1].quantity == 2);
}

TEST_CASE("A non-stackable adds a distinct entry each time", "[inventory]")
{
    Registry r = makeRegistry();
    ItemDef stone;
    stone.id = "stone";
    stone.category = Category::Keepsake;
    stone.stackable = false;
    r.defs["stone"] = stone;

    Satchel s;
    add(s, r, ItemInstance{"stone"});
    add(s, r, ItemInstance{"stone"});
    REQUIRE(count(s, "stone") == 2);
    REQUIRE(s.items.size() == 2); // two entries, not a merged stack
}

TEST_CASE("remove is all-or-nothing and drains across stacks", "[inventory]")
{
    const Registry r = makeRegistry();
    Satchel s;
    add(s, r, ItemInstance{"herb", 7}); // two stacks: 5 + 2

    // Asking for more than present removes nothing and returns false.
    REQUIRE_FALSE(remove(s, "herb", 8));
    REQUIRE(count(s, "herb") == 7);

    // Removing 6 drains the first stack fully and part of the second; empties prune.
    REQUIRE(remove(s, "herb", 6));
    REQUIRE(count(s, "herb") == 1);
    REQUIRE(s.items.size() == 1);

    REQUIRE(remove(s, "herb", 1));
    REQUIRE_FALSE(has(s, "herb"));
    REQUIRE(s.items.empty());
}

TEST_CASE("An unknown item id is treated as non-stackable (safe default)", "[inventory]")
{
    const Registry r = makeRegistry();
    Satchel s;
    add(s, r, ItemInstance{"mystery", 3}); // no def -> each add is a distinct entry
    add(s, r, ItemInstance{"mystery", 1});
    REQUIRE(count(s, "mystery") == 4);
    REQUIRE(s.items.size() == 2); // not merged (unknown -> non-stackable)
}

TEST_CASE("load reads one-JSON-per-item from a directory", "[inventory]")
{
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "wayworn_inv_test";
    fs::remove_all(dir);
    fs::create_directories(dir);

    {
        std::ofstream(dir / "notebook.json")
            << R"({"id":"notebook","name":"Worn Notebook","category":"key_item","rarity":3})";
        std::ofstream(dir / "herb.json")
            << R"({"name":"Wild Thyme","category":"practical","stackable":true,"max_stack":99})";
        std::ofstream(dir / "ignore.txt") << "not json";
    }

    Registry r;
    load(r, dir.string());
    REQUIRE(r.defs.size() == 2); // the .txt is ignored

    const ItemDef* nb = r.find("notebook");
    REQUIRE(nb != nullptr);
    REQUIRE(nb->name == "Worn Notebook");
    REQUIRE(nb->category == Category::KeyItem);
    REQUIRE(nb->rarity == 3);
    REQUIRE_FALSE(nb->stackable);

    // id falls back to the filename stem when the JSON omits "id".
    const ItemDef* herb = r.find("herb");
    REQUIRE(herb != nullptr);
    REQUIRE(herb->category == Category::Practical);
    REQUIRE(herb->stackable);
    REQUIRE(herb->max_stack == 99);

    fs::remove_all(dir);
}
