#include "Scene.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

#include <catch2/catch_test_macros.hpp>

namespace
{
// A temp scenes dir: one well-formed scene, one missing its completion flag
// (which must be dropped -- the flag is the once-guard).
std::string writeSceneDir()
{
    const auto dir = std::filesystem::temp_directory_path() / "wayworn_scene_fixture";
    std::filesystem::create_directories(dir);
    std::ofstream(dir / "good.json") << R"({
        "id": "good",
        "level": "Room",
        "set_flag": "good_done",
        "start_when": [{ "flag": "ready" }],
        "steps": [
            { "wait": 0.5 },
            { "enter": "mom", "at": "door", "facing": "south" },
            { "move": "mom", "to": "bedside" },
            { "face": "mom", "dir": "west" },
            { "observe": "greeting" },
            { "menu": "greeting" },
            { "leave": "mom" },
            { "set_flag": "extra" }
        ]
    })";
    std::ofstream(dir / "no_flag.json") << R"({
        "id": "no_flag", "level": "Room",
        "steps": [{ "wait": 1.0 }]
    })";
    std::ofstream(dir / "paced.json") << R"({
        "id": "paced", "level": "Room", "set_flag": "paced_done",
        "steps": [
            { "fade_in": 4.0 },
            { "remark": "door_call" },
            { "observe": "waking", "blocking": false },
            { "menu": "waking", "must_choose": true, "blocking": false }
        ]
    })";
    return dir.string();
}
} // namespace

TEST_CASE("scenes load with ordered steps; a scene without set_flag is dropped", "[scene]")
{
    scene::Registry reg;
    scene::load(reg, writeSceneDir());
    REQUIRE(reg.scenes.size() == 2);
    const auto it = std::find_if(reg.scenes.begin(), reg.scenes.end(),
                                 [](const scene::Def& s) { return s.id == "good"; });
    REQUIRE(it != reg.scenes.end());
    const scene::Def& d = *it;
    REQUIRE(d.id == "good");
    REQUIRE(d.level == "Room");
    REQUIRE(d.set_flag == "good_done");
    REQUIRE_FALSE(d.start_when.any.empty());

    using K = scene::Step::Kind;
    REQUIRE(d.steps.size() == 8);
    REQUIRE(d.steps[0].kind == K::Wait);
    REQUIRE(d.steps[1].kind == K::Enter);
    REQUIRE(d.steps[1].who == "mom");
    REQUIRE(d.steps[1].target == "door");
    REQUIRE(d.steps[2].kind == K::Move);
    REQUIRE(d.steps[2].target == "bedside");
    REQUIRE(d.steps[3].kind == K::Face);
    REQUIRE(d.steps[3].facing == "west");
    REQUIRE(d.steps[4].kind == K::Observe);
    REQUIRE(d.steps[5].kind == K::Menu);
    REQUIRE(d.steps[5].target == "greeting");
    REQUIRE(d.steps[6].kind == K::Leave);
    REQUIRE(d.steps[7].kind == K::SetFlag);
    REQUIRE(d.steps[7].target == "extra");
    // Pacing defaults: box steps block, menus are leaveable.
    REQUIRE(d.steps[4].blocking);
    REQUIRE(d.steps[5].blocking);
    REQUIRE_FALSE(d.steps[5].must_choose);
}

TEST_CASE("fade_in, blocking and must_choose parse", "[scene]")
{
    scene::Registry reg;
    scene::load(reg, writeSceneDir());
    const auto it = std::find_if(reg.scenes.begin(), reg.scenes.end(),
                                 [](const scene::Def& s) { return s.id == "paced"; });
    REQUIRE(it != reg.scenes.end());
    using K = scene::Step::Kind;
    REQUIRE(it->steps.size() == 4);
    REQUIRE(it->steps[0].kind == K::FadeIn);
    REQUIRE(it->steps[0].seconds == 4.0f);
    REQUIRE(it->steps[1].kind == K::Remark);
    REQUIRE(it->steps[1].target == "door_call");
    REQUIRE(it->steps[1].blocking); // remarks block by default, like observes
    REQUIRE(it->steps[2].kind == K::Observe);
    REQUIRE_FALSE(it->steps[2].blocking);
    REQUIRE(it->steps[3].kind == K::Menu);
    REQUIRE(it->steps[3].must_choose);
    // must_choose forces blocking even when authored non-blocking: the menu
    // repeats until a deed consumes, so the scene cannot advance on first choice.
    REQUIRE(it->steps[3].blocking);
}
