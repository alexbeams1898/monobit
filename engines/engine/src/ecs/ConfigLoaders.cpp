#include "ecs/ConfigLoaders.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <system_error>

using json = nlohmann::json;

namespace engine::ecs
{

namespace
{

ItemCategory parseCategory(const std::string& s)
{
    if (s == "weapon")
        return ItemCategory::Weapon;
    if (s == "armor")
        return ItemCategory::Armor;
    if (s == "consumable")
        return ItemCategory::Consumable;
    if (s == "key_item")
        return ItemCategory::KeyItem;
    if (s == "money")
        return ItemCategory::Money;
    if (s == "accessory")
        return ItemCategory::Accessory;
    if (s == "incantation")
        return ItemCategory::Incantation;
    if (s == "invocation")
        return ItemCategory::Invocation;
    return ItemCategory::Material;
}

Rarity parseRarity(const std::string& s)
{
    if (s == "very_common")
        return Rarity::VeryCommon;
    if (s == "uncommon")
        return Rarity::Uncommon;
    if (s == "rare")
        return Rarity::Rare;
    if (s == "epic")
        return Rarity::Epic;
    if (s == "legendary")
        return Rarity::Legendary;
    return Rarity::Common;
}

ArmorSlot parseArmorSlot(const std::string& s)
{
    if (s == "head")
        return ArmorSlot::Head;
    if (s == "legs")
        return ArmorSlot::Legs;
    if (s == "feet")
        return ArmorSlot::Feet;
    return ArmorSlot::Chest;
}

void loadHpConfig(const json& j, FormulaConfig& f)
{
    if (!j.contains("hp"))
        return;
    f.hp.base = j["hp"].value("base", f.hp.base);
    f.hp.scale = j["hp"].value("scale", f.hp.scale);
    f.hp.level_scale = j["hp"].value("level_scale", f.hp.level_scale);
}

void loadMovementConfig(const json& j, FormulaConfig& f)
{
    if (!j.contains("movement"))
        return;
    const auto& m = j["movement"];
    f.movement.base = m.value("base", f.movement.base);
    f.movement.dex_scale = m.value("dex_scale", f.movement.dex_scale);
    f.movement.sprint_multiplier = m.value("sprint_multiplier", f.movement.sprint_multiplier);
    f.movement.sprint_blend = m.value("sprint_blend", f.movement.sprint_blend);
    f.movement.walk_blend = m.value("walk_blend", f.movement.walk_blend);
    f.movement.backpedal_multiplier =
        m.value("backpedal_multiplier", f.movement.backpedal_multiplier);
    f.movement.sprint_anim_speed = m.value("sprint_anim_speed", f.movement.sprint_anim_speed);
    f.movement.backpedal_anim_speed =
        m.value("backpedal_anim_speed", f.movement.backpedal_anim_speed);
}

void loadCarryWeightConfig(const json& j, FormulaConfig& f)
{
    if (!j.contains("carry_weight"))
        return;
    f.carry_weight.str_scale = j["carry_weight"].value("str_scale", f.carry_weight.str_scale);
    f.carry_weight.end_scale = j["carry_weight"].value("end_scale", f.carry_weight.end_scale);
}

void loadDefenseConfig(const json& j, FormulaConfig& f)
{
    if (!j.contains("defense"))
        return;
    f.defense.str_scale = j["defense"].value("str_scale", f.defense.str_scale);
    f.defense.end_scale = j["defense"].value("end_scale", f.defense.end_scale);
    f.defense.level_scale = j["defense"].value("level_scale", f.defense.level_scale);
    f.defense.cap = j["defense"].value("cap", f.defense.cap);
}

void loadLuckConfig(const json& j, FormulaConfig& f)
{
    if (!j.contains("luck"))
        return;
    f.luck.drop_scale = j["luck"].value("drop_scale", f.luck.drop_scale);
    f.luck.quality_scale = j["luck"].value("quality_scale", f.luck.quality_scale);
    f.luck.essence_quality_scale =
        j["luck"].value("essence_quality_scale", f.luck.essence_quality_scale);
    if (j["luck"].contains("quality_thresholds") && j["luck"]["quality_thresholds"].is_array())
    {
        const auto& qt = j["luck"]["quality_thresholds"];
        for (int i = 0; i < 4 && i < static_cast<int>(qt.size()); ++i)
            f.luck.quality_thresholds[i] = qt[i].get<float>();
    }
}

void loadGradeThresholdsConfig(const json& j, FormulaConfig& f)
{
    if (!j.contains("damage") || !j["damage"].contains("grade_thresholds"))
        return;
    const auto& gt = j["damage"]["grade_thresholds"];
    f.grade_thresholds.s = gt.value("S", f.grade_thresholds.s);
    f.grade_thresholds.a = gt.value("A", f.grade_thresholds.a);
    f.grade_thresholds.b = gt.value("B", f.grade_thresholds.b);
    f.grade_thresholds.c = gt.value("C", f.grade_thresholds.c);
    f.grade_thresholds.d = gt.value("D", f.grade_thresholds.d);
    f.grade_thresholds.e = gt.value("E", f.grade_thresholds.e);
}

void loadSwingConfig(const json& j, FormulaConfig& f)
{
    if (!j.contains("swing"))
        return;
    f.swing.base_swing_time = j["swing"].value("base_swing_time", f.swing.base_swing_time);
    f.swing.weight_scale = j["swing"].value("weight_scale", f.swing.weight_scale);
    f.swing.stat_scale = j["swing"].value("stat_scale", f.swing.stat_scale);
    f.swing.two_handed_str_bonus =
        j["swing"].value("two_handed_str_bonus", f.swing.two_handed_str_bonus);
}

void loadStatRequirementConfig(const json& j, FormulaConfig& f)
{
    if (!j.contains("stat_requirement"))
        return;
    f.stat_requirement.penalty_rate =
        j["stat_requirement"].value("penalty_rate", f.stat_requirement.penalty_rate);
}

void loadLevelingConfig(const json& j, FormulaConfig& f)
{
    if (!j.contains("leveling"))
        return;
    f.leveling.xp_base = j["leveling"].value("xp_base", f.leveling.xp_base);
    f.leveling.xp_exponent = j["leveling"].value("xp_exponent", f.leveling.xp_exponent);
    f.leveling.xp_offset = j["leveling"].value("xp_offset", f.leveling.xp_offset);
    f.leveling.points_per_level =
        j["leveling"].value("points_per_level", f.leveling.points_per_level);
}

void loadPoiseConfig(const json& j, FormulaConfig& f)
{
    if (!j.contains("poise"))
        return;
    f.poise.end_scale = j["poise"].value("end_scale", f.poise.end_scale);
    f.poise.str_scale = j["poise"].value("str_scale", f.poise.str_scale);
    f.poise.weight_scale = j["poise"].value("weight_scale", f.poise.weight_scale);
    f.poise.stagger_duration = j["poise"].value("stagger_duration", f.poise.stagger_duration);
    f.poise.decay_window = j["poise"].value("decay_window", f.poise.decay_window);
}

void loadGatherConfig(const json& j, FormulaConfig& f)
{
    if (!j.contains("gather"))
        return;
    f.gather.wood_gather_respawn_seconds =
        j["gather"].value("wood_gather_respawn_seconds", f.gather.wood_gather_respawn_seconds);
}

void loadDodgeConfig(const json& j, FormulaConfig& f)
{
    if (!j.contains("dodge"))
        return;
    f.dodge.duration = j["dodge"].value("duration", f.dodge.duration);
    f.dodge.cooldown = j["dodge"].value("cooldown", f.dodge.cooldown);
}

void loadEssenceConfig(const json& j, FormulaConfig& f)
{
    if (!j.contains("essence"))
        return;
    f.essence.min = j["essence"].value("min", f.essence.min);
    f.essence.max = j["essence"].value("max", f.essence.max);
}

void loadXpDropConfig(const json& j, FormulaConfig& f)
{
    if (!j.contains("xp_drop"))
        return;
    f.xp_drop.log_scale = j["xp_drop"].value("log_scale", f.xp_drop.log_scale);
    f.xp_drop.min_fraction = j["xp_drop"].value("min_fraction", f.xp_drop.min_fraction);
    f.xp_drop.level_penalty = j["xp_drop"].value("level_penalty", f.xp_drop.level_penalty);
    f.xp_drop.essence_scale = j["xp_drop"].value("essence_scale", f.xp_drop.essence_scale);
}

void loadStaminaConfig(const json& j, FormulaConfig& f)
{
    if (!j.contains("stamina"))
        return;
    const auto& s = j["stamina"];
    f.stamina.base_swing_cost = s.value("base_swing_cost", f.stamina.base_swing_cost);
    f.stamina.swing_effort = s.value("swing_effort", f.stamina.swing_effort);
    f.stamina.dodge_effort = s.value("dodge_effort", f.stamina.dodge_effort);
    f.stamina.skill_effort = s.value("skill_effort", f.stamina.skill_effort);
    f.stamina.sprint_effort = s.value("sprint_effort", f.stamina.sprint_effort);
    f.stamina.jump_effort = s.value("jump_effort", f.stamina.jump_effort);
    f.stamina.sprint_dex_scale = s.value("sprint_dex_scale", f.stamina.sprint_dex_scale);
    f.stamina.base = s.value("base", f.stamina.base);
    f.stamina.end_scale = s.value("end_scale", f.stamina.end_scale);
    f.stamina.recovery_rate = s.value("recovery_rate", f.stamina.recovery_rate);
    f.stamina.recovery_delay = s.value("recovery_delay", f.stamina.recovery_delay);
    f.stamina.exhaustion_stagger = s.value("exhaustion_stagger", f.stamina.exhaustion_stagger);
}

void loadFistConfig(const json& j, FormulaConfig& f)
{
    if (!j.contains("fist"))
        return;
    f.fist.weight = j["fist"].value("weight", f.fist.weight);
    f.fist.base_damage = j["fist"].value("base_damage", f.fist.base_damage);
    f.fist.str_scaling = j["fist"].value("str_scaling", f.fist.str_scaling);
    f.fist.dex_scaling = j["fist"].value("dex_scaling", f.fist.dex_scaling);
}

void loadWeaponXpConfig(const json& j, FormulaConfig& f)
{
    if (!j.contains("weapon_xp"))
        return;
    const auto& wx = j["weapon_xp"];
    f.weapon_xp.kill_multiplier = wx.value("kill_multiplier", f.weapon_xp.kill_multiplier);
    f.weapon_xp.hit_multiplier = wx.value("hit_multiplier", f.weapon_xp.hit_multiplier);
    f.weapon_xp.crit_multiplier = wx.value("crit_multiplier", f.weapon_xp.crit_multiplier);
    f.weapon_xp.base_xp = wx.value("base_xp", f.weapon_xp.base_xp);
    f.weapon_xp.exponent = wx.value("exponent", f.weapon_xp.exponent);
    f.weapon_xp.growth_bonus_per_quality =
        wx.value("growth_bonus_per_quality", f.weapon_xp.growth_bonus_per_quality);
    f.weapon_xp.decay_rate = wx.value("decay_rate", f.weapon_xp.decay_rate);
    f.weapon_xp.carry_factor = wx.value("carry_factor", f.weapon_xp.carry_factor);
    f.weapon_xp.power_level_weight = wx.value("power_level_weight", f.weapon_xp.power_level_weight);
    f.weapon_xp.power_hp_weight = wx.value("power_hp_weight", f.weapon_xp.power_hp_weight);
    f.weapon_xp.power_dmg_weight = wx.value("power_dmg_weight", f.weapon_xp.power_dmg_weight);
    f.weapon_xp.power_stat_weight = wx.value("power_stat_weight", f.weapon_xp.power_stat_weight);
    f.weapon_xp.power_base = wx.value("power_base", f.weapon_xp.power_base);
    f.weapon_xp.power_dmg_factor = wx.value("power_dmg_factor", f.weapon_xp.power_dmg_factor);
    f.weapon_xp.power_rarity_factor =
        wx.value("power_rarity_factor", f.weapon_xp.power_rarity_factor);
}

void loadEquipLoadConfig(const json& j, FormulaConfig& f)
{
    if (!j.contains("equip_load"))
        return;
    const auto& el = j["equip_load"];
    f.equip_load.base_capacity = el.value("base_capacity", f.equip_load.base_capacity);
    f.equip_load.str_scale = el.value("str_scale", f.equip_load.str_scale);
    f.equip_load.end_scale = el.value("end_scale", f.equip_load.end_scale);
    f.equip_load.light_threshold = el.value("light_threshold", f.equip_load.light_threshold);
    f.equip_load.medium_threshold = el.value("medium_threshold", f.equip_load.medium_threshold);
    f.equip_load.heavy_threshold = el.value("heavy_threshold", f.equip_load.heavy_threshold);
    f.equip_load.light_speed = el.value("light_speed", f.equip_load.light_speed);
    f.equip_load.medium_speed = el.value("medium_speed", f.equip_load.medium_speed);
    f.equip_load.heavy_speed = el.value("heavy_speed", f.equip_load.heavy_speed);
    f.equip_load.overloaded_speed = el.value("overloaded_speed", f.equip_load.overloaded_speed);
}

void loadCombatAIConfig(const json& j, FormulaConfig& f)
{
    if (!j.contains("combat_ai"))
        return;
    const auto& ca = j["combat_ai"];
    f.combat_ai.max_attack_tokens = ca.value("max_attack_tokens", f.combat_ai.max_attack_tokens);
    f.combat_ai.wait_radius_mult = ca.value("wait_radius_mult", f.combat_ai.wait_radius_mult);
    f.combat_ai.waiter_speed_scale = ca.value("waiter_speed_scale", f.combat_ai.waiter_speed_scale);
    f.combat_ai.kite_speed_threshold =
        ca.value("kite_speed_threshold", f.combat_ai.kite_speed_threshold);
    f.combat_ai.chase_spread = ca.value("chase_spread", f.combat_ai.chase_spread);
    f.combat_ai.slot_rotation_speed =
        ca.value("slot_rotation_speed", f.combat_ai.slot_rotation_speed);
    f.combat_ai.min_slot_gap = ca.value("min_slot_gap", f.combat_ai.min_slot_gap);
    f.combat_ai.attack_arrival_dist =
        ca.value("attack_arrival_dist", f.combat_ai.attack_arrival_dist);
    f.combat_ai.slot_arrive_dist = ca.value("slot_arrive_dist", f.combat_ai.slot_arrive_dist);
    f.combat_ai.engagement_radius = ca.value("engagement_radius", f.combat_ai.engagement_radius);
    f.combat_ai.enemy_reach = ca.value("enemy_reach", f.combat_ai.enemy_reach);
}

void loadCombatConfig(const json& j, FormulaConfig& f)
{
    if (!j.contains("combat"))
        return;
    const auto& cb = j["combat"];
    f.combat.attack_lock_fraction = cb.value("attack_lock_fraction", f.combat.attack_lock_fraction);
    f.combat.normal_reach = cb.value("normal_reach", f.combat.normal_reach);
    f.combat.skill_reach = cb.value("skill_reach", f.combat.skill_reach);
    f.combat.normal_hitbox_size = cb.value("normal_hitbox_size", f.combat.normal_hitbox_size);
    f.combat.skill_hitbox_size = cb.value("skill_hitbox_size", f.combat.skill_hitbox_size);
    f.combat.skill_damage_mult = cb.value("skill_damage_mult", f.combat.skill_damage_mult);
    f.combat.skill_cooldown = cb.value("skill_cooldown", f.combat.skill_cooldown);
    f.combat.skill_lock_duration = cb.value("skill_lock_duration", f.combat.skill_lock_duration);
    f.combat.dodge_speed = cb.value("dodge_speed", f.combat.dodge_speed);
    f.combat.parry_window = cb.value("parry_window", f.combat.parry_window);
    f.combat.backstab_threshold = cb.value("backstab_threshold", f.combat.backstab_threshold);
    f.combat.backstab_multiplier = cb.value("backstab_multiplier", f.combat.backstab_multiplier);
    f.combat.riposte_multiplier = cb.value("riposte_multiplier", f.combat.riposte_multiplier);
    f.combat.riposte_window = cb.value("riposte_window", f.combat.riposte_window);
    f.combat.critical_lock_duration =
        cb.value("critical_lock_duration", f.combat.critical_lock_duration);
    f.combat.lock_on_range = cb.value("lock_on_range", f.combat.lock_on_range);
}

EvolutionNode parseEvolutionNode(const json& node_json)
{
    EvolutionNode node;
    node.weapon_config_path = node_json.value("weapon", std::string{});

    if (node_json.contains("evolutions") && node_json["evolutions"].is_array())
    {
        for (const auto& ej : node_json["evolutions"])
        {
            EvolutionPath path;
            path.target_node = ej.value("target", std::string{});
            path.min_level = ej.value("min_level", 1);
            path.material_config_path = ej.value("material", std::string{});
            path.material_qty = ej.value("material_qty", 1);
            node.evolutions.push_back(std::move(path));
        }
    }
    return node;
}

} // namespace

bool loadFormulaConfig(FormulaConfig& f, const std::string& file_path)
{
    std::ifstream file(file_path);
    if (!file.is_open())
    {
        std::fprintf(stderr, "[engine::ecs] cannot open formulas: %s -- using defaults\n",
                     file_path.c_str());
        return false;
    }

    json j;
    try
    {
        file >> j;
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "[engine::ecs] error parsing %s: %s -- using defaults\n",
                     file_path.c_str(), e.what());
        return false;
    }

    loadHpConfig(j, f);
    loadMovementConfig(j, f);
    loadCarryWeightConfig(j, f);
    loadDefenseConfig(j, f);
    loadLuckConfig(j, f);
    loadGradeThresholdsConfig(j, f);
    loadSwingConfig(j, f);
    loadStatRequirementConfig(j, f);
    loadLevelingConfig(j, f);
    loadPoiseConfig(j, f);
    loadGatherConfig(j, f);
    loadDodgeConfig(j, f);
    loadEssenceConfig(j, f);
    loadXpDropConfig(j, f);
    loadStaminaConfig(j, f);
    loadFistConfig(j, f);
    loadWeaponXpConfig(j, f);
    loadEquipLoadConfig(j, f);
    loadCombatAIConfig(j, f);
    loadCombatConfig(j, f);

    f.loaded = true;
    std::fprintf(stderr, "[engine::ecs] loaded formulas from %s\n", file_path.c_str());
    return true;
}

int loadItemRegistry(ItemRegistry& registry, const std::string& dir_path)
{
    namespace fs = std::filesystem;
    int count = 0;
    std::error_code ec;

    for (const auto& entry : fs::recursive_directory_iterator(dir_path, ec))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".json")
            continue;

        std::ifstream file(entry.path());
        if (!file.is_open())
            continue;

        json j;
        try
        {
            file >> j;
        }
        catch (const std::exception& e)
        {
            std::fprintf(stderr, "[engine::ecs] error parsing item %s: %s\n",
                         entry.path().string().c_str(), e.what());
            continue;
        }

        const std::string config_path = entry.path().generic_string();

        ItemDef def;
        def.config_path = config_path;
        def.name = j.value("name", std::string{});
        def.description = j.value("description", std::string{});
        def.icon_path = j.value("icon", std::string{});
        def.category = parseCategory(j.value("category", std::string{"material"}));
        def.rarity = parseRarity(j.value("rarity", std::string{"common"}));

        def.base_damage = j.value("base_damage", 0.0f);
        def.weight = j.value("weight", 0.5f);
        def.str_scaling = j.value("str_scaling", 0.0f);
        def.dex_scaling = j.value("dex_scaling", 0.0f);
        def.end_scaling = j.value("end_scaling", 0.0f);
        def.lck_scaling = j.value("lck_scaling", 0.0f);
        def.per_scaling = j.value("per_scaling", 0.0f);
        def.cog_scaling = j.value("cog_scaling", 0.0f);
        def.int_scaling = j.value("int_scaling", 0.0f);
        def.str_requirement = j.value("str_requirement", 0);
        def.dex_requirement = j.value("dex_requirement", 0);
        def.end_requirement = j.value("end_requirement", 0);
        def.lck_requirement = j.value("lck_requirement", 0);
        def.per_requirement = j.value("per_requirement", 0);
        def.cog_requirement = j.value("cog_requirement", 0);
        def.int_requirement = j.value("int_requirement", 0);
        def.two_handed = j.value("two_handed", false);
        def.weapon_tier = j.value("weapon_tier", std::string{});
        def.damage_per_level = j.value("damage_per_level", -1.0f);
        def.scaling_per_level = j.value("scaling_per_level", -1.0f);
        def.weapon_class_id = j.value("weapon_class_id", std::string{});

        def.ranged = j.value("ranged", false);
        def.projectile_speed = j.value("projectile_speed", 400.0f);
        def.effective_range = j.value("effective_range", 500.0f);
        def.magazine_size = j.value("magazine_size", 0);
        def.reload_time = j.value("reload_time", 1.5f);
        def.spread = j.value("spread", 0.0f);
        def.projectile_count = j.value("projectile_count", 1);
        def.projectile_size = j.value("projectile_size", 6.0f);
        def.pierce = j.value("pierce", 0);
        def.projectile_sprite = j.value("projectile_sprite", std::string{});
        def.ammo_type = j.value("ammo_type", std::string{});
        def.fire_sound = j.value("fire_sound", std::string{});
        def.fire_rate = j.value("fire_rate", 0.0f);
        def.stamina_cost = j.value("stamina_cost", -1.0f);

        def.visual_weapon = j.value("visual_weapon", std::string{});
        def.world_mesh = j.value("world_mesh", std::string{});
        def.weapon_icon = j.value("weapon_icon", std::string{});
        def.grip_x = j.value("grip_x", 0.0f);
        def.grip_y = j.value("grip_y", 0.0f);
        def.grip_offset_x = j.value("grip_offset_x", 0.0f);
        def.grip_offset_y = j.value("grip_offset_y", 0.0f);
        def.grip_offset_z = j.value("grip_offset_z", 0.0f);
        def.grip_rot_deg_x = j.value("grip_rot_deg_x", 0.0f);
        def.grip_rot_deg_y = j.value("grip_rot_deg_y", 0.0f);
        def.grip_rot_deg_z = j.value("grip_rot_deg_z", 0.0f);
        def.grip_scale = j.value("grip_scale", 1.0f);
        def.attack_icon_ns = j.value("attack_icon_ns", std::string{});
        def.attack_grip_ns_x = j.value("attack_grip_ns_x", 0.0f);
        def.attack_grip_ns_y = j.value("attack_grip_ns_y", 0.0f);
        def.attack_fore_grip_ns_x = j.value("attack_fore_grip_ns_x", 0.0f);
        def.attack_fore_grip_ns_y = j.value("attack_fore_grip_ns_y", 0.0f);
        def.fore_grip_x = j.value("fore_grip_x", def.grip_x);
        def.fore_grip_y = j.value("fore_grip_y", def.grip_y);
        def.weapon_scale = j.value("weapon_scale", 1.0f);
        def.base_rotation = j.value("base_rotation", 0.0f) * 3.14159265f / 180.0f;
        def.attack_anim = j.value("attack_anim", std::string{});
        if (j.contains("shoot_frames"))
        {
            for (const auto& fr : j["shoot_frames"])
                def.shoot_frames.push_back(fr.get<int>());
        }

        def.armor_slot = parseArmorSlot(j.value("armor_slot", std::string{"chest"}));
        def.defense_bonus = j.value("defense_bonus", 0.0f);
        def.poise_bonus = j.value("poise_bonus", 0.0f);
        def.max_guard = j.value("max_guard", 0.0f);
        def.parry_window = j.value("parry_window", 0.15f);

        def.str_bonus = j.value("str_bonus", 0);
        def.dex_bonus = j.value("dex_bonus", 0);
        def.end_bonus = j.value("end_bonus", 0);
        def.lck_bonus = j.value("lck_bonus", 0);

        def.max_durability = j.value("max_durability", 100.0f);
        def.stackable = j.value("stackable", false);
        def.max_stack = j.value("max_stack", 1);
        def.value = j.value("value", 0);

        registry.defs[config_path] = std::move(def);
        ++count;
    }

    if (ec)
        std::fprintf(stderr, "[engine::ecs] error scanning item dir %s: %s\n", dir_path.c_str(),
                     ec.message().c_str());

    registry.loaded = (count > 0);
    std::fprintf(stderr, "[engine::ecs] loaded %d item definitions from %s\n", count,
                 dir_path.c_str());
    return count;
}

int loadRecipeRegistry(RecipeRegistry& registry, const std::string& dir_path)
{
    namespace fs = std::filesystem;
    int count = 0;
    std::error_code ec;

    for (const auto& entry : fs::recursive_directory_iterator(dir_path, ec))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".json")
            continue;

        std::ifstream file(entry.path());
        if (!file.is_open())
            continue;

        json j;
        try
        {
            file >> j;
        }
        catch (const std::exception& e)
        {
            std::fprintf(stderr, "[engine::ecs] error parsing recipe %s: %s\n",
                         entry.path().string().c_str(), e.what());
            continue;
        }

        RecipeDef recipe;
        recipe.config_path = entry.path().generic_string();
        recipe.name = j.value("name", std::string{});
        recipe.output_item = j.value("output", std::string{});
        recipe.output_quantity = j.value("output_quantity", 1);
        recipe.sangue_cost = j.value("sangue_cost", 0);
        recipe.substrate = j.value("substrate", std::string{});

        if (j.contains("inputs") && j["inputs"].is_array())
        {
            for (const auto& ij : j["inputs"])
            {
                RecipeIngredient ing;
                ing.config_path = ij.value("item", std::string{});
                ing.quantity = ij.value("quantity", 1);
                recipe.inputs.push_back(std::move(ing));
            }
        }

        registry.recipes.push_back(std::move(recipe));
        ++count;
    }

    if (ec)
        std::fprintf(stderr, "[engine::ecs] error scanning recipe dir %s: %s\n", dir_path.c_str(),
                     ec.message().c_str());

    registry.loaded = (count > 0);
    std::fprintf(stderr, "[engine::ecs] loaded %d recipes from %s\n", count, dir_path.c_str());
    return count;
}

bool loadWeaponTierRegistry(WeaponTierRegistry& registry, const std::string& file_path)
{
    std::ifstream file(file_path);
    if (!file.is_open())
    {
        std::fprintf(stderr, "[engine::ecs] no weapon tiers file: %s -- using defaults\n",
                     file_path.c_str());
        return false;
    }

    json j;
    try
    {
        file >> j;
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "[engine::ecs] error parsing %s: %s\n", file_path.c_str(), e.what());
        return false;
    }

    int count = 0;
    for (auto it = j.begin(); it != j.end(); ++it)
    {
        WeaponTierDef td;
        td.damage_per_level = it.value().value("damage_per_level", 1.0f);
        td.scaling_per_level = it.value().value("scaling_per_level", 0.02f);
        td.xp_rate = it.value().value("xp_rate", 1.0f);
        registry.tiers[it.key()] = td;
        ++count;
    }

    registry.loaded = (count > 0);
    std::fprintf(stderr, "[engine::ecs] loaded %d weapon tiers from %s\n", count,
                 file_path.c_str());
    return count > 0;
}

int loadEvolutionRegistry(EvolutionRegistry& registry, const std::string& dir_path)
{
    namespace fs = std::filesystem;
    int count = 0;
    std::error_code ec;

    for (const auto& entry : fs::directory_iterator(dir_path, ec))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".json")
            continue;

        std::ifstream file(entry.path());
        if (!file.is_open())
            continue;

        json j;
        try
        {
            file >> j;
        }
        catch (const std::exception& e)
        {
            std::fprintf(stderr, "[engine::ecs] error parsing evolution tree %s: %s\n",
                         entry.path().string().c_str(), e.what());
            continue;
        }

        EvolutionFamily family;
        family.name = j.value("family", std::string{});

        if (j.contains("nodes") && j["nodes"].is_object())
        {
            for (auto it = j["nodes"].begin(); it != j["nodes"].end(); ++it)
                family.nodes[it.key()] = parseEvolutionNode(it.value());
        }

        const int family_idx = static_cast<int>(registry.families.size());
        for (const auto& [node_id, node] : family.nodes)
        {
            if (!node.weapon_config_path.empty())
                registry.weapon_to_node[node.weapon_config_path] = {family_idx, node_id};
        }

        registry.families.push_back(std::move(family));
        ++count;
    }

    if (ec)
        std::fprintf(stderr, "[engine::ecs] evolution dir not found: %s\n", dir_path.c_str());

    registry.loaded = (count > 0);
    if (count > 0)
        std::fprintf(stderr, "[engine::ecs] loaded %d evolution trees from %s\n", count,
                     dir_path.c_str());
    return count;
}

} // namespace engine::ecs
