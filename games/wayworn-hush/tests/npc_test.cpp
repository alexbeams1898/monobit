#include "Npc.h"
#include "ecs/Components.h"
#include "ecs/EntityManager.h"

#include <filesystem>
#include <fstream>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;

namespace
{
// A temp config dir with one authored character, exercising the same
// directory-loading path the game uses at boot.
std::string writeNpcDir()
{
    const auto dir = std::filesystem::temp_directory_path() / "wayworn_npc_fixture";
    std::filesystem::create_directories(dir);
    std::ofstream f(dir / "mom.json");
    f << R"({
        "name": "Mom",
        "texture": "assets/sprites/mom_walk.png",
        "frame_width": 64, "frame_height": 64,
        "direction_count": 4, "max_frames_per_state": 9,
        "idle": { "row": 1, "frames": 1, "duration": 0.0 },
        "collider": { "w": 22.0, "h": 12.0 }
    })";
    return dir.string();
}
} // namespace

TEST_CASE("npc configs load from a directory (file stem = fallback id)", "[npc]")
{
    npc::Registry reg;
    npc::load(reg, writeNpcDir());
    REQUIRE(reg.npcs.count("mom") == 1);
    const npc::Config& c = reg.npcs.at("mom");
    REQUIRE(c.name == "Mom");
    REQUIRE(c.texture == "assets/sprites/mom_walk.png");
    REQUIRE(c.idle_row == 1);
    REQUIRE(c.collider_w == Approx(22.0f));
}

TEST_CASE("a spawned npc stands solid, idle, facing as authored", "[npc]")
{
    npc::Registry regs;
    npc::load(regs, writeNpcDir());

    EntityManager em;
    const entt::entity e = npc::spawn(em, regs.npcs.at("mom"), 100.0f, 200.0f, "west");
    auto& reg = em.registry();

    REQUIRE(reg.get<Transform>(e).x == Approx(100.0f));
    REQUIRE(reg.get<Transform>(e).y == Approx(200.0f));
    REQUIRE(reg.get<Collider>(e).is_solid); // the player cannot walk through a person
    REQUIRE(reg.get<Sprite>(e).layer == 2);
    REQUIRE(reg.get<Animation>(e).current_row == 1); // idle
    REQUIRE(reg.get<FacingDirection>(e).render_dx == Approx(-1.0f));
    REQUIRE(reg.get<FacingDirection>(e).render_dy == Approx(0.0f));
}

namespace
{
// A creature with a full life: named anims, a routine, a schedule with a gated
// night entry -- the cat's shape, plus the story-reactive case.
std::string writeCatDir()
{
    const auto dir = std::filesystem::temp_directory_path() / "wayworn_npc_life_fixture";
    std::filesystem::create_directories(dir);
    std::ofstream f(dir / "cat.json");
    f << R"({
        "texture": "assets/sprites/cat_walk.png",
        "anims": { "sit": { "row": 1, "frames": 1, "duration": 0.0 } },
        "routines": {
            "laze": [ { "wander": 96 }, { "anim": "sit" }, { "wait": [3.0, 9.0] } ],
            "prowl": [ { "move_to": "hall" }, { "face": "north" }, { "wait": 2.0 } ]
        },
        "schedule": [
            { "when": { "from": "22:00", "to": "06:00",
                        "unlock_when": [ { "flag": "cat_trusts_you" } ] },
              "routine": "prowl" },
            { "when": { "from": "00:00", "to": "24:00" }, "routine": "laze" },
            { "when": { "from": "nonsense" }, "routine": "laze" },
            { "when": { "from": "08:00", "to": "10:00" }, "routine": "no_such_routine" }
        ]
    })";
    return dir.string();
}
} // namespace

TEST_CASE("routines, anims and schedule parse; bad entries drop loudly", "[npc]")
{
    npc::Registry reg;
    npc::load(reg, writeCatDir());
    const npc::Config& c = reg.npcs.at("cat");

    REQUIRE(c.anims.at("sit").row == 1);
    REQUIRE(c.routines.at("laze").size() == 3);
    REQUIRE(c.routines.at("laze")[0].kind == npc::RoutineStep::Kind::Wander);
    REQUIRE(c.routines.at("laze")[0].radius == Approx(96.0f));
    REQUIRE(c.routines.at("laze")[2].wait_min == Approx(3.0f));
    REQUIRE(c.routines.at("laze")[2].wait_max == Approx(9.0f));
    REQUIRE(c.routines.at("prowl")[0].target == "hall");

    // The malformed-time entry and the unknown-routine entry are dropped.
    REQUIRE(c.schedule.size() == 2);
}

TEST_CASE("the schedule picks the first entry whose window and gate hold", "[npc]")
{
    npc::Registry reg;
    npc::load(reg, writeCatDir());
    const npc::Config& c = reg.npcs.at("cat");

    const auto never = [](const unlock::Condition&) { return false; };
    const auto gateless = [](const unlock::Condition& cond) { return cond.any.empty(); };

    // Midday: only the all-day entry's window holds.
    const auto* noon = npc::activeEntry(c, 0.5, gateless);
    REQUIRE(noon != nullptr);
    REQUIRE(noon->routine == "laze");

    // 23:00 -- inside the wrapping night window, but its gate needs the flag.
    const auto* nightUntrusted = npc::activeEntry(c, 23.0 / 24.0, gateless);
    REQUIRE(nightUntrusted->routine == "laze");
    const auto* nightTrusted =
        npc::activeEntry(c, 23.0 / 24.0, [](const unlock::Condition&) { return true; });
    REQUIRE(nightTrusted->routine == "prowl");

    // A gate that never holds leaves nobody scheduled.
    REQUIRE(npc::activeEntry(c, 0.5, never) == nullptr);
}
