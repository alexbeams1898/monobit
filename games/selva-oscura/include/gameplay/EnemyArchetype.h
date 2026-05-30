#pragma once

#include "combat/HurtboxDecl.h"
#include "gameplay/Perception.h"

#include <nlohmann/json_fwd.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace selva::gameplay
{

// One thing an enemy can do — a clip to play, a range it's valid at,
// a cooldown, a weight for random selection. Loaded from JSON; never
// constructed by hand at runtime. Sprint 4's behavior tree's
// LeafPickAction filters the actor's actions[] by range_min..range_max
// + cooldown + min_awareness, then weighted-randoms within what
// remains, then fires the chosen action's clip via playOneShot.
//
// Schema is intentionally narrow for v1 — anything an action might
// also want (animation overrides, sound, damage scaling) can land on
// this struct as fields later without breaking the file format
// (nlohmann's WITH_DEFAULT serialization tolerates missing keys).
struct EnemyAction
{
    std::string id;   // unique key within archetype, e.g. "shade_swing"
    std::string clip; // ozz clip name to play
    float range_min = 0.0f;
    float range_max = 0.0f; // 0 = no upper limit
    float cooldown_seconds = 0.0f;
    float weight = 1.0f;
    int raw_damage = 0;
    int poise_damage = 0;
    float blend_in_seconds = 0.10f;
    float blend_out_seconds = 0.20f;
    bool freeze_last = false;
    Awareness min_awareness = Awareness::Combat;
    // Hitbox geometry — mirrors the player's WeaponAttack schema.
    // Empty hitbox_joint = action plays its clip but spawns no
    // hitbox (e.g. a roar, wind-up taunt). Common case: joint is
    // set, swing fires a hitbox; LeafPickAction calls
    // selva::combat::spawnAttackHitbox.
    std::string hitbox_joint;
    float hitbox_radius = 0.18f;
    float hitbox_tip_offset_z = 0.0f;
};

// One enemy archetype = a list of actions + perception overrides.
// Loaded from config/enemies/<id>.json. Actor.archetype points at
// one of these (or nullptr for the test-dummy fallback).
//
// Perception fields are std::optional — present in JSON means
// "override the global tunable for this archetype." Absent means
// "use the global value." That lets a fast scout enemy override
// vision_range while a slow shade inherits the default.
struct EnemyArchetype
{
    std::string id;
    std::vector<EnemyAction> actions;
    std::optional<float> vision_fov_degrees;
    std::optional<float> vision_range_meters;
    // Which behavior tree drives this archetype's decisions. Tree
    // construction is in code (see BehaviorTree.cpp's tree-builder
    // registry); JSON just names which one to bind. Defaults to
    // "humanoid_basic" — covers every humanoid in the bestiary
    // until a tree-specific behavior demands its own builder.
    std::string tree_id = "humanoid_basic";
    // Skeleton key (matches SkeletalAssets registry: "player" for
    // humanoids reusing the X_Bot rig; "wolf" / etc. for distinct
    // skeletons). Default "player" preserves today's behavior --
    // every existing humanoid shade reuses the player rig.
    std::string skeleton_id = "player";
    // Per-archetype clip names. Empty = "use the default for this
    // skeleton" (the hardcoded humanoid defaults in Enemies.cpp).
    // Wolf overrides these to its own clip names.
    std::string idle_clip;       // empty -> "standard_idle"
    std::string combat_idle_clip; // empty -> "unarmed_combat_idle"
    std::string walk_clip;       // empty -> "walking"
    std::string death_clip;      // empty -> "death"
    std::string knockdown_clip;  // empty -> "stunned"
    // Per-archetype hurtbox layout. Empty -> the archetype loads
    // the player's hurtboxes (every humanoid shade today). Non-empty
    // overrides (e.g. wolf authors its own 4-or-5 capsules).
    std::vector<selva::combat::HurtboxDecl> hurtbox_decls;
};

// nlohmann JSON I/O for these structs. Defined in EnemyArchetype.cpp
// to keep this header from pulling the full json.hpp.
void to_json(nlohmann::json& j, const EnemyAction& a);
void from_json(const nlohmann::json& j, EnemyAction& a);
void to_json(nlohmann::json& j, const EnemyArchetype& a);
void from_json(const nlohmann::json& j, EnemyArchetype& a);

// Process-wide archetype registry. Loaded once at startup; read by
// gameplay code via archetypes(). Sprint 3 ships with one entry —
// "limbo_shade" — but the registry scales to as many archetypes as
// the bestiary has files for.
class EnemyArchetypeRegistry
{
  public:
    // Load every *.json in `dir` as an archetype. Files whose JSON
    // parse fails are logged and skipped — never throws.
    void loadDirectory(const std::filesystem::path& dir);

    // nullptr if no archetype with that id was loaded.
    const EnemyArchetype* get(const std::string& id) const;

    // Read-only view of the registry. Used by F1 diagnostic panel.
    const std::unordered_map<std::string, EnemyArchetype>& all() const
    {
        return by_id;
    }

  private:
    std::unordered_map<std::string, EnemyArchetype> by_id;
};

// Single global registry. Pattern matches selva::anim::clips().
EnemyArchetypeRegistry& archetypes();

} // namespace selva::gameplay
