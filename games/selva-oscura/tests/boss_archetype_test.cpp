// Boss-backend data layer tests: EnemyArchetype JSON round-trip with
// all new boss fields. Per
// games/selva-oscura/docs/design/ideas/boss_backend.md sections 1, 8,
// 9, 11, plus the Pattern B initial_state / engage_clip fields.
//
// Purpose: guard against silent field drift -- if a JSON key gets
// renamed in code, the round-trip fails loudly here instead of the
// game booting Lupa without `is_boss=true` and her behaving like a
// normal mob.
//
// Pure-compute test; no GL, no Jolt, no SDL.

#include "gameplay/EnemyArchetype.h"

#include <nlohmann/json.hpp>

#include <catch2/catch_test_macros.hpp>

using selva::gameplay::EnemyArchetype;

TEST_CASE("EnemyArchetype: defaults are non-boss", "[boss-backend]")
{
    const EnemyArchetype a;
    REQUIRE_FALSE(a.is_boss);
    REQUIRE(a.boss_name.empty());
    REQUIRE(a.encounter_audio_bed.empty());
    REQUIRE(a.felled_message.empty());
    REQUIRE(a.initial_state.empty());
    REQUIRE(a.engage_clip.empty());
}

TEST_CASE("EnemyArchetype: from_json reads boss fields", "[boss-backend]")
{
    const nlohmann::json src = {
        {"id", "wolf"},
        {"is_boss", true},
        {"boss_name", "LUPA"},
        {"encounter_audio_bed", "lupa_encounter"},
        {"felled_message", "lupa e' caduta"},
        {"initial_state", "sitting"},
        {"engage_clip", "jump_to_idle"},
    };
    EnemyArchetype a = src.get<EnemyArchetype>();

    REQUIRE(a.id == "wolf");
    REQUIRE(a.is_boss);
    REQUIRE(a.boss_name == "LUPA");
    REQUIRE(a.encounter_audio_bed == "lupa_encounter");
    REQUIRE(a.felled_message == "lupa e' caduta");
    REQUIRE(a.initial_state == "sitting");
    REQUIRE(a.engage_clip == "jump_to_idle");
}

TEST_CASE("EnemyArchetype: non-boss archetype has no boss-field bleed", "[boss-backend]")
{
    // A typical non-boss archetype JSON (limbo_shade style) MUST parse
    // with all boss fields defaulted false / empty even though the JSON
    // omits them entirely.
    const nlohmann::json src = {
        {"id", "limbo_shade"},
        {"tree", "humanoid_basic"},
    };
    const EnemyArchetype a = src.get<EnemyArchetype>();

    REQUIRE_FALSE(a.is_boss);
    REQUIRE(a.boss_name.empty());
    REQUIRE(a.encounter_audio_bed.empty());
    REQUIRE(a.felled_message.empty());
    REQUIRE(a.initial_state.empty());
    REQUIRE(a.engage_clip.empty());
}

TEST_CASE("EnemyArchetype: round-trip preserves boss fields", "[boss-backend]")
{
    EnemyArchetype a;
    a.id = "wolf";
    a.is_boss = true;
    a.boss_name = "LUPA";
    a.encounter_audio_bed = "lupa_encounter";
    a.felled_message = "the slope remembers her";
    a.initial_state = "sitting";
    a.engage_clip = "jump_to_idle";

    const nlohmann::json j = a;
    const EnemyArchetype b = j.get<EnemyArchetype>();

    REQUIRE(b.id == a.id);
    REQUIRE(b.is_boss == a.is_boss);
    REQUIRE(b.boss_name == a.boss_name);
    REQUIRE(b.encounter_audio_bed == a.encounter_audio_bed);
    REQUIRE(b.felled_message == a.felled_message);
    REQUIRE(b.initial_state == a.initial_state);
    REQUIRE(b.engage_clip == a.engage_clip);
}

TEST_CASE("EnemyArchetype: loot_drops round-trips", "[boss-backend][loot-drops]")
{
    // Lupa-shape archetype: no sangue_drop (Animal-form actors don't
    // yield Hell-substance), three loot drops (organic loot) each
    // with their own chance + quantity range.
    EnemyArchetype a;
    a.id = "wolf";
    a.loot_drops = {
        {"config/items/materials/lupa_meat.json", 1, 2, 0.95f},
        {"config/items/materials/lupa_bone.json", 1, 1, 0.50f},
        {"config/items/materials/lupa_hide.json", 1, 1, 0.20f},
    };

    const nlohmann::json j = a;
    REQUIRE(j.contains("loot_drops"));
    REQUIRE(j["loot_drops"].size() == 3);
    REQUIRE_FALSE(j.contains("sangue_drop"));

    const EnemyArchetype b = j.get<EnemyArchetype>();
    REQUIRE(b.loot_drops.size() == 3);
    REQUIRE(b.loot_drops[0].config_path == "config/items/materials/lupa_meat.json");
    REQUIRE(b.loot_drops[0].min_qty == 1);
    REQUIRE(b.loot_drops[0].max_qty == 2);
    REQUIRE(b.loot_drops[0].base_chance == 0.95f);
    REQUIRE(b.loot_drops[1].config_path == "config/items/materials/lupa_bone.json");
    REQUIRE(b.loot_drops[2].config_path == "config/items/materials/lupa_hide.json");
    REQUIRE(b.loot_drops[2].base_chance == 0.20f);
    REQUIRE(b.sangue_drop == 0u);
}

TEST_CASE("EnemyArchetype: loot_drops defaults to empty", "[boss-backend][loot-drops]")
{
    const EnemyArchetype a;
    REQUIRE(a.loot_drops.empty());

    // A typical Hell-side archetype JSON (sangue only, no loot_drops):
    const nlohmann::json src = {{"id", "limbo_shade"}, {"sangue_drop", 1}};
    const EnemyArchetype b = src.get<EnemyArchetype>();
    REQUIRE(b.sangue_drop == 1u);
    REQUIRE(b.loot_drops.empty());
}

TEST_CASE("EnemyArchetype: to_json omits empty boss fields for non-bosses", "[boss-backend]")
{
    // Non-boss serialization should be compact: empty boss fields are
    // not emitted, keeping the on-disk archetype JSONs clean.
    EnemyArchetype a;
    a.id = "limbo_shade";
    const nlohmann::json j = a;

    REQUIRE_FALSE(j.contains("is_boss"));
    REQUIRE_FALSE(j.contains("boss_name"));
    REQUIRE_FALSE(j.contains("encounter_audio_bed"));
    REQUIRE_FALSE(j.contains("felled_message"));
    REQUIRE_FALSE(j.contains("initial_state"));
    REQUIRE_FALSE(j.contains("engage_clip"));
}
