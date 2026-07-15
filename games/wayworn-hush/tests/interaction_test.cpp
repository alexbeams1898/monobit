#include "Interaction.h"
#include "Inventory.h"
#include "Loot.h"
#include "ecs/Components.h"
#include "ecs/EntityManager.h"

#include <catch2/catch_test_macros.hpp>

using interaction::Candidate;
using interaction::Intent;
using interaction::resolve;

namespace
{
// A 32x32 box centered at (x,y).
Candidate box(float x, float y)
{
    return {x, y, 32.0f, 32.0f};
}

// A Context with empty observation/growth state -- enough to dispatch a direct action
// (which only touches satchel + items + loot). rng is unused by a plain Pickup.
const observations::RollRng kNoRng = [](int) { return 0; };

// An actionable-only interactable (no observe_id) at the origin: a direct Pickup/Gather.
interaction::Interactable actionAt(interaction::ActionKind kind, const std::string& target)
{
    interaction::Interactable inter{};
    inter.w = 24.0f;
    inter.h = 24.0f;
    inter.action = kind;
    inter.target = target;
    return inter;
}
} // namespace

TEST_CASE("Nothing targeted when the player is out of reach and no hover", "[interaction]")
{
    const std::vector<Candidate> items = {box(500, 500)};
    Intent it;
    it.px = 0;
    it.py = 0;
    const auto r = resolve(items, it, 40.0f);
    REQUIRE(r.index == -1);
    REQUIRE_FALSE(r.fire);
}

TEST_CASE("Proximity targets the nearest box within reach", "[interaction]")
{
    const std::vector<Candidate> items = {box(0, 0), box(200, 0)};
    Intent it;
    it.px = 30;
    it.py = 0; // near box 0 (edge at 16), far from box 1
    const auto r = resolve(items, it, 40.0f);
    REQUIRE(r.index == 0);
}

TEST_CASE("Pressing fires the proximity target", "[interaction]")
{
    const std::vector<Candidate> items = {box(0, 0)};
    Intent it;
    it.px = 0;
    it.py = 0;
    it.pressed = true;
    const auto r = resolve(items, it, 40.0f);
    REQUIRE(r.index == 0);
    REQUIRE(r.fire);
}

TEST_CASE("Hover beats proximity: the cursor overrides where you stand", "[interaction]")
{
    // Player stands in box 0's reach; cursor hovers box 1 (also within player reach).
    const std::vector<Candidate> items = {box(0, 0), box(40, 0)};
    Intent it;
    it.px = 0;
    it.py = 0;
    it.mouse_valid = true;
    it.mouse_x = 40; // inside box 1
    it.mouse_y = 0;
    const auto r = resolve(items, it, 60.0f);
    REQUIRE(r.index == 1); // hover wins
}

TEST_CASE("A click fires only when it landed on a hovered interactable, not empty space",
          "[interaction]")
{
    const std::vector<Candidate> items = {box(0, 0)};
    Intent it;
    it.px = 0;
    it.py = 0;
    it.clicked = true;

    SECTION("click over the box (hovering) -> fires")
    {
        it.mouse_valid = true;
        it.mouse_x = 0;
        it.mouse_y = 0; // inside the box
        const auto r = resolve(items, it, 40.0f);
        REQUIRE(r.index == 0);
        REQUIRE(r.fire);
    }
    SECTION("click in empty space (no hover) -> targets by proximity but does NOT fire")
    {
        it.mouse_valid = true;
        it.mouse_x = 500;
        it.mouse_y = 500; // not over any box
        const auto r = resolve(items, it, 40.0f);
        REQUIRE(r.index == 0); // still the proximity target
        REQUIRE_FALSE(r.fire); // but a click in the void doesn't fire it
    }
}

TEST_CASE("A cursor hovering something out of the player's reach does not target it",
          "[interaction]")
{
    // Cursor is over box 1, but the player is far from it -> hover is gated by reach.
    const std::vector<Candidate> items = {box(0, 0), box(1000, 0)};
    Intent it;
    it.px = 0;
    it.py = 0; // near box 0 only
    it.mouse_valid = true;
    it.mouse_x = 1000;
    it.mouse_y = 0; // over box 1, far from player
    const auto r = resolve(items, it, 40.0f);
    REQUIRE(r.index == 0); // falls back to the in-reach proximity target
}

TEST_CASE("Firing an actionable-only Pickup deposits the item and despawns it directly",
          "[interaction]")
{
    EntityManager em;
    auto& reg = em.registry();

    inventory::Registry items;
    items.defs["river_stone"] = inventory::ItemDef{"river_stone", "Worn River Stone"};
    const entt::entity pickup = reg.create();
    reg.emplace<Transform>(pickup, Transform{0.0f, 0.0f});
    reg.emplace<interaction::Interactable>(
        pickup, actionAt(interaction::ActionKind::Pickup, "river_stone"));

    observations::State obs;
    growth::GrowthState growth;
    inventory::Satchel satchel;
    loot::Registry loot;
    interaction::Context ctx{obs, growth, kNoRng, satchel, items, loot, 40.0f};

    Intent it;
    it.px = 0;
    it.py = 0;
    it.pressed = true; // Space, in reach -> fires directly (no menu)

    const interaction::Outcome out = interaction::update(em, it, ctx);
    REQUIRE(out.fired);
    REQUIRE(out.observe_target.empty()); // a direct action, not an observe
    REQUIRE(out.items.size() == 1);
    REQUIRE(out.items[0].id == "river_stone");
    // The item is now carried, and the world item is gone (taken).
    REQUIRE(inventory::count(satchel, "river_stone") == 1);
    REQUIRE_FALSE(reg.valid(pickup));
}

TEST_CASE("An item that is only targeted (not fired) stays in the world", "[interaction]")
{
    EntityManager em;
    auto& reg = em.registry();

    inventory::Registry items;
    items.defs["river_stone"] = inventory::ItemDef{"river_stone", "Worn River Stone"};
    const entt::entity pickup = reg.create();
    reg.emplace<Transform>(pickup, Transform{0.0f, 0.0f});
    reg.emplace<interaction::Interactable>(
        pickup, actionAt(interaction::ActionKind::Pickup, "river_stone"));

    observations::State obs;
    growth::GrowthState growth;
    inventory::Satchel satchel;
    loot::Registry loot;
    interaction::Context ctx{obs, growth, kNoRng, satchel, items, loot, 40.0f};

    Intent it; // in reach, but no press/click
    it.px = 0;
    it.py = 0;

    const interaction::Outcome out = interaction::update(em, it, ctx);
    REQUIRE_FALSE(out.fired);
    REQUIRE(reg.valid(pickup));                                 // still there
    REQUIRE(reg.get<interaction::Interactable>(pickup).active); // highlighted (in reach)
    REQUIRE(inventory::count(satchel, "river_stone") == 0);
}

TEST_CASE("An observable-AND-actionable spot observes (the take is a deed, not direct)",
          "[interaction]")
{
    EntityManager em;
    auto& reg = em.registry();

    // A spot carrying BOTH an observe_id and a direct action. Observable wins: interacting
    // opens the reading; the action does NOT fire directly (its "take" would be a deed).
    inventory::Registry items;
    items.defs["river_stone"] = inventory::ItemDef{"river_stone", "Worn River Stone"};
    const entt::entity spot = reg.create();
    reg.emplace<Transform>(spot, Transform{0.0f, 0.0f});
    interaction::Interactable inter{};
    inter.w = 24.0f;
    inter.h = 24.0f;
    inter.observe_id = "stone_spot";
    inter.action = interaction::ActionKind::Pickup;
    inter.target = "river_stone";
    reg.emplace<interaction::Interactable>(spot, inter);

    observations::State obs;
    growth::GrowthState growth;
    inventory::Satchel satchel;
    loot::Registry loot;
    interaction::Context ctx{obs, growth, kNoRng, satchel, items, loot, 40.0f};

    Intent it;
    it.px = 0;
    it.py = 0;
    it.pressed = true;

    const interaction::Outcome out = interaction::update(em, it, ctx);
    REQUIRE(out.fired);
    REQUIRE(out.observe_target == "stone_spot"); // observed, not taken
    REQUIRE(out.items.empty());                  // no direct deposit
    REQUIRE(inventory::count(satchel, "river_stone") == 0);
    REQUIRE(reg.valid(spot)); // an observation persists (re-readable)
}

TEST_CASE("Firing an actionable-only Gather rolls the table into the satchel and despawns it",
          "[interaction]")
{
    EntityManager em;
    auto& reg = em.registry();

    // A single-entry table with a fixed 2 rolls and qty 1 each -> deterministic 2 thyme.
    inventory::Registry items;
    items.defs["wild_thyme"] = inventory::ItemDef{"wild_thyme", "Wild Thyme"};
    items.defs["wild_thyme"].stackable = true;
    items.defs["wild_thyme"].max_stack = 99;
    loot::Registry loot;
    loot::Table table;
    table.id = "herbs";
    table.rolls_min = 2;
    table.rolls_max = 2;
    table.entries.push_back(loot::Entry{"wild_thyme", 1, 1, 1});
    loot.tables["herbs"] = table;

    const entt::entity node = reg.create();
    reg.emplace<Transform>(node, Transform{0.0f, 0.0f});
    reg.emplace<interaction::Interactable>(node,
                                           actionAt(interaction::ActionKind::Gather, "herbs"));

    observations::State obs;
    growth::GrowthState growth;
    inventory::Satchel satchel;
    interaction::Context ctx{obs, growth, kNoRng, satchel, items, loot, 40.0f};

    Intent it;
    it.px = 0;
    it.py = 0;
    it.pressed = true;

    const interaction::Outcome out = interaction::update(em, it, ctx);
    REQUIRE(out.fired);
    REQUIRE(out.observe_target.empty());
    REQUIRE(inventory::count(satchel, "wild_thyme") == 2); // 2 rolls x 1 each
    REQUIRE_FALSE(reg.valid(node));                        // node consumed
}

TEST_CASE("A Gather naming an unknown table fires but deposits nothing", "[interaction]")
{
    EntityManager em;
    auto& reg = em.registry();

    const inventory::Registry items;
    const loot::Registry loot; // empty -- "ghost" table id resolves to nothing
    const entt::entity node = reg.create();
    reg.emplace<Transform>(node, Transform{0.0f, 0.0f});
    reg.emplace<interaction::Interactable>(node,
                                           actionAt(interaction::ActionKind::Gather, "ghost"));

    observations::State obs;
    growth::GrowthState growth;
    inventory::Satchel satchel;
    interaction::Context ctx{obs, growth, kNoRng, satchel, items, loot, 40.0f};

    Intent it;
    it.px = 0;
    it.py = 0;
    it.pressed = true;

    const interaction::Outcome out = interaction::update(em, it, ctx);
    REQUIRE(out.fired);
    REQUIRE(out.items.empty());
    REQUIRE_FALSE(reg.valid(node)); // still consumed (a one-shot node), just empty-handed
}
