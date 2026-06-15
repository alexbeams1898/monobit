#pragma once

// All balance formulas live here as pure data. Loaded once at game start
// from a JSON file (each game picks its own path) and read-only during
// gameplay. The formula SHAPE is portable across games; only the constants
// differ -- each game overrides defaults via its JSON config.
//
// To use:
//   1. Construct FormulaConfig.
//   2. Call engine::ecs::loadFormulaConfig(cfg, "path/to/formulas.json")
//      from the game's startup code.
//   3. Read cfg.<sub-struct>.<field> wherever you need a constant.

namespace engine::ecs
{

struct FormulaConfig
{
    // Player HP formula: hp = base + (max_hp_stat * scale) + (level * level_scale).
    struct
    {
        float base = 5.f;
        float scale = 15.f;
        float level_scale = 5.f;
    } hp;

    // Movement speed + sprint/walk blending.
    struct
    {
        float base = 150.f;
        float dex_scale = 30.f;
        float sprint_multiplier = 1.6f;
        float sprint_blend = 8.0f;
        float walk_blend = 20.0f;
        float backpedal_multiplier = 0.5f;
        float sprint_anim_speed = 0.65f;
        float backpedal_anim_speed = 1.4f;
    } movement;

    // Carry weight cap.
    struct
    {
        float str_scale = 20.f;
        float end_scale = 10.f;
    } carry_weight;

    // Damage mitigation from armor + stats. Capped to avoid 100%-immune.
    struct
    {
        float str_scale = 0.3f;
        float end_scale = 0.5f;
        float level_scale = 0.2f;
        float cap = 75.f;
    } defense;

    // Drop quantity + quality scaling from Luck stat. Per-quality
    // thresholds drive how high a roll bumps the quality tier.
    struct
    {
        float drop_scale = 15.f;
        float quality_scale = 3.f;
        float essence_quality_scale = 0.01f;
        float quality_thresholds[4] = {55.f, 85.f, 102.f, 120.f};
    } luck;

    // Per-hit grade thresholds for damage scoring.
    struct
    {
        float s = 1.5f;
        float a = 1.25f;
        float b = 1.0f;
        float c = 0.75f;
        float d = 0.5f;
        float e = 0.25f;
    } grade_thresholds;

    // Swing time formula: base + weight*weight_scale - stat_total/stat_scale.
    struct
    {
        float base_swing_time = 0.8f;
        float weight_scale = 0.5f;
        float stat_scale = 80.f;
        float two_handed_str_bonus = 0.3f;
    } swing;

    // Damage penalty when wielder's stats don't meet weapon requirements.
    struct
    {
        float penalty_rate = 0.15f;
    } stat_requirement;

    // Poise formula. Stagger duration is fixed; decay_window is seconds
    // before poise begins refilling after the last hit.
    struct
    {
        float end_scale = 2.0f;
        float str_scale = 1.0f;
        float weight_scale = 3.0f;
        float stagger_duration = 0.15f;
        float decay_window = 5.0f;
    } poise;

    // XP-to-level curve and points-per-level allocation.
    struct
    {
        float xp_base = 0.069f;
        float xp_exponent = 3.5f;
        float xp_offset = 7.f;
        float points_per_level = 1.f;
    } leveling;

    // XP awarded for enemy kills + level-difference falloff.
    struct
    {
        float log_scale = 1.5f;
        float min_fraction = 0.1f;
        float level_penalty = 0.15f;
        float essence_scale = 0.005f;
    } xp_drop;

    // Dodge timing.
    struct
    {
        float duration = 0.25f;
        float cooldown = 0.35f;
    } dodge;

    // Player "essence" stat range (currency-ish system; per-game opt-in).
    struct
    {
        int min = 0;
        int max = 100;
    } essence;

    // Stamina costs + recovery. The stamina formula is:
    //   max = base + (END * end_scale)
    //   recovery: starts after recovery_delay, ticks at recovery_rate/s.
    // Per-action costs scale with weapon weight via the *_effort fields.
    struct
    {
        float base_swing_cost = 3.0f;
        float swing_effort = 0.5f;
        float dodge_effort = 2.5f;
        float skill_effort = 4.0f;
        float sprint_effort = 2.0f;
        float jump_effort = 6.0f; // flat per-jump cost; soulslike convention
        float sprint_dex_scale = 0.15f;
        float base = 10.0f;
        float end_scale = 20.0f;
        float recovery_rate = 8.0f;
        float recovery_delay = 1.0f;
        float exhaustion_stagger = 0.6f;
    } stamina;

    // Unarmed (fist) attack defaults. Body.unarmed_* overrides per-entity.
    struct
    {
        float weight = 0.5f;
        float base_damage = 5.0f;
        float str_scaling = 1.0f;
        float dex_scaling = 0.75f;
    } fist;

    // Weapon XP gain on hit/kill + weapon-power scoring. Used to scale
    // how fast a given weapon levels up based on damage + rarity.
    struct
    {
        float kill_multiplier = 1.0f;
        float hit_multiplier = 0.05f;
        float crit_multiplier = 0.3f;
        float base_xp = 50.0f;
        float exponent = 2.0f;
        float growth_bonus_per_quality = 0.1f;
        float decay_rate = 0.05f;
        float carry_factor = 0.15f;
        float power_base = 0.1f;
        float power_dmg_factor = 0.03f;
        float power_rarity_factor = 0.1f;
        // Enemy power-rating weights (used to score the "earner" enemy).
        float power_level_weight = 1.0f;
        float power_hp_weight = 0.1f;
        float power_dmg_weight = 0.5f;
        float power_stat_weight = 0.2f;
    } weapon_xp;

    // Equip load. carrying_weight / capacity → load_ratio → tier (light/
    // medium/heavy/overloaded) → movement-speed multiplier.
    struct
    {
        float base_capacity = 40.0f;
        float str_scale = 3.0f;
        float end_scale = 1.5f;
        float light_threshold = 0.3f;
        float medium_threshold = 0.7f;
        float heavy_threshold = 1.0f;
        float light_speed = 1.0f;
        float medium_speed = 0.9f;
        float heavy_speed = 0.7f;
        float overloaded_speed = 0.4f;
    } equip_load;

    // Combat AI tuning -- engagement-radius / slot-rotation defaults
    // appropriate for top-down combat; each game can override.
    struct
    {
        int max_attack_tokens = 2;
        float wait_radius_mult = 2.0f;
        float waiter_speed_scale = 0.15f;
        float kite_speed_threshold = 0.5f;
        float chase_spread = 0.15f;
        float slot_rotation_speed = 0.5f;
        float min_slot_gap = 0.8f;
        float attack_arrival_dist = 16.0f;
        float slot_arrive_dist = 24.0f;
        float engagement_radius = 150.0f;
        float enemy_reach = 24.0f;
    } combat_ai;

    // Gather-node respawn cadence. Real-time seconds between trickle
    // spawns when a gather flow is below its active_cap. Single global
    // value drives every gather flow today; per-flow override TBD if a
    // specific material needs slower regrowth.
    struct
    {
        float wood_gather_respawn_seconds = 30.0f;
    } gather;

    // Heal percentages per tier (multiplied against max HP).
    // Per [[project_healing_system_locked_2026_06_14]]: Poultice 20% /
    // Salve 35% / Electuary 55% / Theriac 80%. Per-tier % so the
    // numbers scale automatically as the HP cap grows -- no retuning
    // when END/HP curves shift.
    struct
    {
        float poultice_pct = 0.20f;
        float salve_pct = 0.35f;
        float electuary_pct = 0.55f;
        float theriac_pct = 0.80f;
    } heal;

    // Core combat constants (attack lock, reach, backstab, parry/riposte).
    struct
    {
        float attack_lock_fraction = 0.6f;
        float normal_reach = 36.0f;
        float skill_reach = 56.0f;
        float normal_hitbox_size = 32.0f;
        float skill_hitbox_size = 64.0f;
        float skill_damage_mult = 1.5f;
        float skill_cooldown = 5.0f;
        float skill_lock_duration = 0.4f;
        float dodge_speed = 300.0f;
        float parry_window = 0.15f;
        float backstab_threshold = -0.3f;
        float backstab_multiplier = 2.0f;
        float riposte_multiplier = 2.5f;
        float riposte_window = 0.8f;
        float critical_lock_duration = 0.6f;
        float lock_on_range = 300.0f;
    } combat;

    bool loaded = false;
};

} // namespace engine::ecs
