// Per-class soft-cap curves per
// [[project_class_stats_v2_locked_2026_06_14]]. Verifies the curve
// math: below-bend full returns, above-bend fractional returns, and
// the no-cap / no-config bypass paths (PlayerClass::None, unknown
// derived key, multiplier >= 1.0 all return the input unchanged).

#include "AppState.h"
#include "softcaps/SoftCaps.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cstdio>
#include <filesystem>

using Catch::Matchers::WithinAbs;

namespace
{

// Write a minimal curve config for two classes -- Penitent bend-late
// (good at END), Heretic bend-early (bad at END). Mirrors the v1
// authoring pattern in config/balance/soft_caps.json. Returns path.
std::string writeStandardConfig(const std::string& tag)
{
    namespace fs = std::filesystem;
    const fs::path dir = "tmp/selva-softcap-tests";
    std::error_code ec;
    fs::create_directories(dir, ec);
    const std::string path = (dir / (tag + ".json")).string();
    FILE* f = std::fopen(path.c_str(), "w");
    REQUIRE(f != nullptr);
    std::fputs(R"({
        "hp_from_end": {
            "penitent": { "bend_at": 40, "post_bend_multiplier": 0.35 },
            "heretic":  { "bend_at": 15, "post_bend_multiplier": 0.20 }
        }
    })",
              f);
    std::fclose(f);
    return path;
}

} // namespace

TEST_CASE("PlayerClass::None bypasses soft-cap (enemies pass raw stat)", "[softcap]")
{
    REQUIRE(selva::softcaps::loadFromFile(writeStandardConfig("none-bypass")));

    // Even with curves loaded, None never applies any cap. Enemies
    // use this path -- their HP is tuned via archetype overrides, not
    // class curves.
    REQUIRE_THAT(selva::softcaps::apply(50.0f, selva::PlayerClass::None, "hp_from_end"),
                 WithinAbs(50.0f, 0.001f));
    REQUIRE_THAT(selva::softcaps::apply(1.0f, selva::PlayerClass::None, "hp_from_end"),
                 WithinAbs(1.0f, 0.001f));
}

TEST_CASE("Below the bend: full returns (multiplier == 1.0 effectively)", "[softcap]")
{
    REQUIRE(selva::softcaps::loadFromFile(writeStandardConfig("below-bend")));

    // Penitent's bend is at 40; at END=30 we're well below it.
    REQUIRE_THAT(selva::softcaps::apply(30.0f, selva::PlayerClass::Penitent, "hp_from_end"),
                 WithinAbs(30.0f, 0.001f));
    // At END=40 (exactly the bend), still full -- the cap fires for
    // values STRICTLY above the bend, not at it.
    REQUIRE_THAT(selva::softcaps::apply(40.0f, selva::PlayerClass::Penitent, "hp_from_end"),
                 WithinAbs(40.0f, 0.001f));

    // Heretic's bend is at 15; at END=10 we're below.
    REQUIRE_THAT(selva::softcaps::apply(10.0f, selva::PlayerClass::Heretic, "hp_from_end"),
                 WithinAbs(10.0f, 0.001f));
}

TEST_CASE("Above the bend: fractional returns per the multiplier", "[softcap]")
{
    REQUIRE(selva::softcaps::loadFromFile(writeStandardConfig("above-bend")));

    // Penitent at END=60: 40 below the bend (full) + 20 above
    // (multiplied by 0.35). Effective = 40 + 20 * 0.35 = 47.
    REQUIRE_THAT(selva::softcaps::apply(60.0f, selva::PlayerClass::Penitent, "hp_from_end"),
                 WithinAbs(47.0f, 0.001f));

    // Heretic at END=50: 15 below the bend + 35 above (multiplied by
    // 0.20). Effective = 15 + 35 * 0.20 = 22.
    REQUIRE_THAT(selva::softcaps::apply(50.0f, selva::PlayerClass::Heretic, "hp_from_end"),
                 WithinAbs(22.0f, 0.001f));

    // Same END=50 returns very different effective values per class.
    // Penitent gets 50 unchanged from above-bend at 40 plus extra:
    // 40 + (50-40)*0.35 = 43.5. Heretic gets 22. The CLASS LENS is
    // the difference; the engine HP formula computes from these
    // class-shifted inputs.
    REQUIRE_THAT(selva::softcaps::apply(50.0f, selva::PlayerClass::Penitent, "hp_from_end"),
                 WithinAbs(43.5f, 0.001f));
}

TEST_CASE("Unknown derived key returns raw stat (forward-compatible)", "[softcap]")
{
    REQUIRE(selva::softcaps::loadFromFile(writeStandardConfig("unknown-key")));

    // Asking for a curve key that doesn't exist (e.g. a future
    // stamina_from_end before its curve has been authored) returns
    // the raw value. The system fails-open: new derived values that
    // haven't been wired yet behave as if no cap applies, which
    // matches the engine's class-agnostic formula behavior.
    REQUIRE_THAT(selva::softcaps::apply(80.0f, selva::PlayerClass::Penitent,
                                        "stamina_from_end"),
                 WithinAbs(80.0f, 0.001f));
}

TEST_CASE("Class missing from a curve block returns raw stat", "[softcap]")
{
    // Config only declares Penitent and Heretic; Ferine and
    // Unburdened aren't in the block. Their curves return raw value.
    REQUIRE(selva::softcaps::loadFromFile(writeStandardConfig("missing-class")));

    REQUIRE_THAT(selva::softcaps::apply(60.0f, selva::PlayerClass::Ferine, "hp_from_end"),
                 WithinAbs(60.0f, 0.001f));
    REQUIRE_THAT(selva::softcaps::apply(60.0f, selva::PlayerClass::Unburdened, "hp_from_end"),
                 WithinAbs(60.0f, 0.001f));
}

TEST_CASE("Multiplier >= 1.0 is a no-op cap (linear forever)", "[softcap]")
{
    // Author a curve whose multiplier is 1.0 -- effectively no cap.
    // Used in soft_caps.json for the Unburdened block (which never
    // fires in practice since their stats are locked at 1, but the
    // curve exists for completeness).
    namespace fs = std::filesystem;
    const fs::path dir = "tmp/selva-softcap-tests";
    std::error_code ec;
    fs::create_directories(dir, ec);
    const std::string path = (dir / "noop-cap.json").string();
    FILE* f = std::fopen(path.c_str(), "w");
    REQUIRE(f != nullptr);
    std::fputs(R"({
        "hp_from_end": {
            "unburdened": { "bend_at": 1, "post_bend_multiplier": 1.0 }
        }
    })",
              f);
    std::fclose(f);
    REQUIRE(selva::softcaps::loadFromFile(path));

    REQUIRE_THAT(
        selva::softcaps::apply(100.0f, selva::PlayerClass::Unburdened, "hp_from_end"),
        WithinAbs(100.0f, 0.001f));
}
