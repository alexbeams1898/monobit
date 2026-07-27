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
