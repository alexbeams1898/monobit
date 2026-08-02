#include "Inventory.h"

#include <cstdio>
#include <filesystem>
#include <fstream>

#include <catch2/catch_test_macros.hpp>

using namespace inventory;

namespace
{
// A registry with a low-cap stackable (cap 5) and a singleton item (cap 1), built in code so the
// op tests don't depend on config files. Every item stacks now; a cap of 1 is how a singleton
// (the notebook) is modeled.
Registry makeRegistry()
{
    Registry r;
    ItemDef herb;
    herb.id = "herb";
    herb.category = Category::Practical;
    herb.max_stack = 5;
    r.defs["herb"] = herb;

    ItemDef key;
    key.id = "notebook";
    key.category = Category::KeyItem;
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

TEST_CASE("A cap-1 item spills into a distinct entry each time", "[inventory]")
{
    Registry r = makeRegistry();
    ItemDef stone;
    stone.id = "stone";
    stone.category = Category::Keepsake;
    stone.max_stack = 1; // a singleton cap -> each add is its own entry
    r.defs["stone"] = stone;

    Satchel s;
    add(s, r, ItemInstance{"stone"});
    add(s, r, ItemInstance{"stone"});
    REQUIRE(count(s, "stone") == 2);
    REQUIRE(s.items.size() == 2); // two entries, cap-1 can't merge
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

TEST_CASE("An unknown item id falls back to cap 1 (safe default)", "[inventory]")
{
    const Registry r = makeRegistry();
    Satchel s;
    add(s, r, ItemInstance{"mystery", 3}); // no def -> cap 1 -> three singleton entries
    add(s, r, ItemInstance{"mystery", 1});
    REQUIRE(count(s, "mystery") == 4);
    REQUIRE(s.items.size() == 4); // no def -> cap 1 -> every unit is its own entry
}

TEST_CASE("load reads one-JSON-per-item from a directory", "[inventory]")
{
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "wayworn_inv_test";
    fs::remove_all(dir);
    fs::create_directories(dir);

    {
        std::ofstream(dir / "notebook.json")
            << R"({"id":"notebook","name":"Worn Notebook","category":"key_item","rarity":3,"max_stack":1})";
        std::ofstream(dir / "herb.json")
            << R"({"name":"Wild Thyme","category":"practical","max_stack":99})";
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
    REQUIRE(nb->max_stack == 1); // singleton

    // id falls back to the filename stem when the JSON omits "id".
    const ItemDef* herb = r.find("herb");
    REQUIRE(herb != nullptr);
    REQUIRE(herb->category == Category::Practical);
    REQUIRE(herb->max_stack == 99);

    // max_stack defaults high (99) when a config omits it -> everything stacks by default.
    std::ofstream(dir / "plain.json") << R"({"id":"plain","name":"Plain","category":"practical"})";
    Registry r2;
    load(r2, dir.string());
    REQUIRE(r2.find("plain")->max_stack == 99);

    fs::remove_all(dir);
}

TEST_CASE("markAllSeen clears the newly-found flag on every item", "[inventory]")
{
    const Registry r;
    Satchel s;
    add(s, r, ItemInstance{"river_stone"});
    add(s, r, ItemInstance{"wild_thyme", 2});
    // Freshly added items are new by default.
    for (const auto& e : s.items)
        REQUIRE(e.is_new);

    markAllSeen(s);
    for (const auto& e : s.items)
        REQUIRE_FALSE(e.is_new);
}

// --- What he has taken up -------------------------------------------------------------

TEST_CASE("only a tool can be held -- this is not a game about holding turnips", "[inventory]")
{
    Registry reg;
    reg.defs["spade"] = ItemDef{"spade", "Spade"};
    reg.defs["spade"].category = Category::Tool;
    reg.defs["branches"] = ItemDef{"branches", "Fallen Branches"};
    reg.defs["branches"].category = Category::Practical;
    reg.defs["notebook"] = ItemDef{"notebook", "Notebook"};
    reg.defs["notebook"].category = Category::KeyItem;

    REQUIRE(equippable(reg, "spade"));
    REQUIRE_FALSE(equippable(reg, "branches"));
    REQUIRE_FALSE(equippable(reg, "notebook")); // an instrument works from the bag
    REQUIRE_FALSE(equippable(reg, "no_such_thing"));

    Satchel s;
    add(s, reg, ItemInstance{"spade"});
    add(s, reg, ItemInstance{"branches", 3});

    REQUIRE(toggleHeld(s, reg, "spade"));
    REQUIRE(s.held == "spade");
    REQUIRE_FALSE(toggleHeld(s, reg, "branches")); // refused, and the hands are undisturbed
    REQUIRE(s.held == "spade");
}

TEST_CASE("taking a thing up and putting it down are one gesture", "[inventory]")
{
    Registry reg;
    reg.defs["spade"] = ItemDef{"spade", "Spade"};
    reg.defs["spade"].category = Category::Tool;
    Satchel s;
    add(s, reg, ItemInstance{"spade"});

    REQUIRE(toggleHeld(s, reg, "spade"));
    REQUIRE(s.held == "spade");
    REQUIRE(toggleHeld(s, reg, "spade"));
    REQUIRE(s.held.empty());
}

TEST_CASE("a tool he does not carry cannot be held", "[inventory]")
{
    Registry reg;
    reg.defs["spade"] = ItemDef{"spade", "Spade"};
    reg.defs["spade"].category = Category::Tool;
    Satchel s; // empty bag
    REQUIRE_FALSE(toggleHeld(s, reg, "spade"));
    REQUIRE(s.held.empty());
}

TEST_CASE("a tool that leaves the bag leaves his hands", "[inventory]")
{
    // The hands and the bag are two records of one fact. Spending, giving away or losing the
    // held thing must not leave `held` naming something he no longer has.
    Registry reg;
    reg.defs["spade"] = ItemDef{"spade", "Spade"};
    reg.defs["spade"].category = Category::Tool;
    Satchel s;
    add(s, reg, ItemInstance{"spade"});
    REQUIRE(toggleHeld(s, reg, "spade"));

    REQUIRE(remove(s, "spade"));
    reconcileHeld(s);
    REQUIRE(s.held.empty());
}
