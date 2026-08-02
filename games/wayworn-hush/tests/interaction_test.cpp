#include "Interaction.h"
#include "Inventory.h"
#include "Yields.h"
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

// An Intent at (px,py) FACING (fx,fy). Proximity targeting requires facing the
// thing (with WASD, facing is the pointer), so a test about distance has to look
// at what it means to target -- the default here faces east, toward +x.
Intent facing(float px, float py, float fx = 1.0f, float fy = 0.0f)
{
    Intent it;
    it.px = px;
    it.py = py;
    it.face_dx = fx;
    it.face_dy = fy;
    return it;
}

// A Context with empty observation/growth state -- enough to dispatch a direct action
// (which only touches satchel + items + yields). rng is unused by a plain Pickup.
const psyche::RollRng kNoRng = [](int) { return 0; };

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
    Intent it = facing(0, 0, 1.0f, 1.0f); // looking toward it -- still far out of reach
    const auto r = resolve(items, it, 40.0f);
    REQUIRE(r.index == -1);
    REQUIRE_FALSE(r.fire);
}

TEST_CASE("Proximity targets the nearest box within reach", "[interaction]")
{
    const std::vector<Candidate> items = {box(0, 0), box(200, 0)};
    // Stand right of box 0 facing WEST, back toward it: near box 0 (edge at 16),
    // far from box 1.
    const Intent it = facing(30, 0, -1.0f, 0.0f);
    const auto r = resolve(items, it, 40.0f);
    REQUIRE(r.index == 0);
}

TEST_CASE("Pressing fires the proximity target", "[interaction]")
{
    const std::vector<Candidate> items = {box(0, 0)};
    Intent it = facing(0, 0); // standing inside the box -- always faced
    it.pressed = true;
    const auto r = resolve(items, it, 40.0f);
    REQUIRE(r.index == 0);
    REQUIRE(r.fire);
}

TEST_CASE("Hover beats proximity: the cursor overrides where you stand", "[interaction]")
{
    // Player stands in box 0's reach; cursor hovers box 1 (also within player reach).
    const std::vector<Candidate> items = {box(0, 0), box(40, 0)};
    Intent it = facing(0, 0);
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
    Intent it = facing(0, 0);
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

    psyche::State obs;
    growth::GrowthState growth;
    inventory::Satchel satchel;
    yields::Registry tables;
    interaction::Context ctx{obs, growth, kNoRng, satchel, items, tables, 40.0f};

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

    psyche::State obs;
    growth::GrowthState growth;
    inventory::Satchel satchel;
    yields::Registry tables;
    interaction::Context ctx{obs, growth, kNoRng, satchel, items, tables, 40.0f};

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

    // A spot carrying BOTH an observe_id and a direct action. Encounter wins: interacting
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

    psyche::State obs;
    growth::GrowthState growth;
    inventory::Satchel satchel;
    yields::Registry tables;
    interaction::Context ctx{obs, growth, kNoRng, satchel, items, tables, 40.0f};

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
    items.defs["wild_thyme"].max_stack = 99;
    yields::Registry tables;
    yields::Table table;
    table.id = "herbs";
    table.rolls_min = 2;
    table.rolls_max = 2;
    table.entries.push_back(yields::Entry{"wild_thyme", 1, 1, 1});
    tables.tables["herbs"] = table;

    const entt::entity node = reg.create();
    reg.emplace<Transform>(node, Transform{0.0f, 0.0f});
    reg.emplace<interaction::Interactable>(node,
                                           actionAt(interaction::ActionKind::Gather, "herbs"));

    psyche::State obs;
    growth::GrowthState growth;
    inventory::Satchel satchel;
    interaction::Context ctx{obs, growth, kNoRng, satchel, items, tables, 40.0f};

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
    const yields::Registry tables; // empty -- "ghost" table id resolves to nothing
    const entt::entity node = reg.create();
    reg.emplace<Transform>(node, Transform{0.0f, 0.0f});
    reg.emplace<interaction::Interactable>(node,
                                           actionAt(interaction::ActionKind::Gather, "ghost"));

    psyche::State obs;
    growth::GrowthState growth;
    inventory::Satchel satchel;
    interaction::Context ctx{obs, growth, kNoRng, satchel, items, tables, 40.0f};

    Intent it;
    it.px = 0;
    it.py = 0;
    it.pressed = true;

    const interaction::Outcome out = interaction::update(em, it, ctx);
    REQUIRE(out.fired);
    REQUIRE(out.items.empty());
    REQUIRE_FALSE(reg.valid(node)); // still consumed (a one-shot node), just empty-handed
}

TEST_CASE("Proximity requires FACING the thing -- what's behind you is not a target",
          "[interaction]")
{
    // With WASD, facing IS the pointer: turning toward a thing is how the player
    // says which thing they mean. A spot at your back is in reach but not chosen.
    const std::vector<Candidate> items = {box(40, 0)}; // to the EAST of the player

    REQUIRE(resolve(items, facing(0, 0, 1.0f, 0.0f), 40.0f).index == 0);   // facing east: targeted
    REQUIRE(resolve(items, facing(0, 0, -1.0f, 0.0f), 40.0f).index == -1); // facing west: dark
    REQUIRE(resolve(items, facing(0, 0, 0.0f, 1.0f), 40.0f).index == -1);  // facing south: dark

    // Standing INSIDE a box is always faced -- there is no "behind" at zero distance.
    REQUIRE(resolve(items, facing(40, 0, -1.0f, 0.0f), 40.0f).index == 0);
}

TEST_CASE("Hovering points at a thing regardless of facing", "[interaction]")
{
    // The cursor is its own pointing: if you put it on the thing, you have said
    // which thing you mean, whichever way the body happens to be turned.
    const std::vector<Candidate> items = {box(40, 0)};
    Intent it = facing(0, 0, -1.0f, 0.0f); // facing AWAY from the box
    it.mouse_valid = true;
    it.mouse_x = 40;
    it.mouse_y = 0; // cursor on it
    const auto r = resolve(items, it, 60.0f);
    REQUIRE(r.index == 0);
}

TEST_CASE("Facing is measured to the box's nearest point, not its center", "[interaction]")
{
    // A wide thing you stand beside (a long counter) reads as ahead when its near
    // edge is ahead, even though its center is off to one side.
    const std::vector<Candidate> items = {{100.0f, 0.0f, 200.0f, 32.0f}}; // spans x 0..200
    // Standing at x=0 facing EAST: the box's center is at x=100 (ahead), and its
    // nearest point is right at the player -- inside, so faced either way.
    REQUIRE(resolve(items, facing(-20, 0, 1.0f, 0.0f), 40.0f).index == 0);
    // Facing WEST from the same spot: the whole box is behind.
    REQUIRE(resolve(items, facing(-20, 0, -1.0f, 0.0f), 40.0f).index == -1);
}
