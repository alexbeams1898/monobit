#include "gameplay/EnemyArchetype.h"

#include "combat/CombatLog.h"

#include <nlohmann/json.hpp>

#include <fstream>

namespace selva::gameplay
{

namespace
{

Awareness parseAwareness(const std::string& s)
{
    if (s == "Unaware")
        return Awareness::Unaware;
    if (s == "Suspicious")
        return Awareness::Suspicious;
    if (s == "Alerted")
        return Awareness::Alerted;
    return Awareness::Combat; // default; also matches "Combat"
}

selva::combat::HurtRegion parseHurtRegion(const std::string& s)
{
    if (s == "Head")
        return selva::combat::HurtRegion::Head;
    if (s == "UpperLimb")
        return selva::combat::HurtRegion::UpperLimb;
    if (s == "LowerLimb")
        return selva::combat::HurtRegion::LowerLimb;
    return selva::combat::HurtRegion::Torso;
}

const char* awarenessString(Awareness a)
{
    switch (a)
    {
    case Awareness::Unaware:
        return "Unaware";
    case Awareness::Suspicious:
        return "Suspicious";
    case Awareness::Alerted:
        return "Alerted";
    case Awareness::Combat:
        break;
    }
    return "Combat";
}

} // namespace

void to_json(nlohmann::json& j, const EnemyAction& a)
{
    j = {{"id", a.id},
         {"clip", a.clip},
         {"range_min", a.range_min},
         {"range_max", a.range_max},
         {"cooldown_seconds", a.cooldown_seconds},
         {"weight", a.weight},
         {"raw_damage", a.raw_damage},
         {"poise_damage", a.poise_damage},
         {"blend_in_seconds", a.blend_in_seconds},
         {"blend_out_seconds", a.blend_out_seconds},
         {"freeze_last", a.freeze_last},
         {"min_awareness", awarenessString(a.min_awareness)},
         {"hitbox_joint", a.hitbox_joint},
         {"hitbox_radius", a.hitbox_radius},
         {"hitbox_tip_offset_z", a.hitbox_tip_offset_z},
         {"windup_seconds", a.windup_seconds},
         {"active_seconds", a.active_seconds},
         {"cancel_fraction", a.cancel_fraction},
         {"locks_movement", a.locks_movement}};
}

void from_json(const nlohmann::json& j, EnemyAction& a)
{
    j.at("id").get_to(a.id);
    j.at("clip").get_to(a.clip);
    a.range_min = j.value("range_min", 0.0f);
    a.range_max = j.value("range_max", 0.0f);
    a.cooldown_seconds = j.value("cooldown_seconds", 0.0f);
    a.weight = j.value("weight", 1.0f);
    a.raw_damage = j.value("raw_damage", 0);
    a.poise_damage = j.value("poise_damage", 0);
    a.blend_in_seconds = j.value("blend_in_seconds", 0.10f);
    a.blend_out_seconds = j.value("blend_out_seconds", 0.20f);
    a.freeze_last = j.value("freeze_last", false);
    a.min_awareness = parseAwareness(j.value("min_awareness", std::string("Combat")));
    a.hitbox_joint = j.value("hitbox_joint", std::string{});
    a.hitbox_radius = j.value("hitbox_radius", 0.18f);
    a.hitbox_tip_offset_z = j.value("hitbox_tip_offset_z", 0.0f);
    a.windup_seconds = j.value("windup_seconds", 0.0f);
    a.active_seconds = j.value("active_seconds", 0.0f);
    a.cancel_fraction = j.value("cancel_fraction", 1.0f);
    a.locks_movement = j.value("locks_movement", true);
}

void to_json(nlohmann::json& j, const EnemyArchetype& a)
{
    j = nlohmann::json{{"id", a.id}, {"actions", a.actions}, {"tree", a.tree_id}};
    if (a.vision_fov_degrees.has_value())
        j["vision_fov_degrees"] = *a.vision_fov_degrees;
    if (a.vision_range_meters.has_value())
        j["vision_range_meters"] = *a.vision_range_meters;
    // Only emit faction when non-default (Hostile is the legacy
    // default; omitting it keeps existing enemy archetypes compact).
    if (a.faction != Faction::Hostile)
        j["faction"] = factionName(a.faction);
    if (a.is_npc)
        j["is_npc"] = true;
    // Only emit form when non-default (DamnedSoul = legacy default).
    if (a.form != Form::DamnedSoul)
        j["form"] = formName(a.form);
    // Skeleton / clip fields — emit so round-trip preserves them.
    if (!a.skeleton_id.empty() && a.skeleton_id != "player")
        j["skeleton_id"] = a.skeleton_id;
    if (!a.idle_clip.empty()) j["idle_clip"] = a.idle_clip;
    if (!a.combat_idle_clip.empty()) j["combat_idle_clip"] = a.combat_idle_clip;
    if (!a.walk_clip.empty()) j["walk_clip"] = a.walk_clip;
    if (!a.walk_back_clip.empty()) j["walk_back_clip"] = a.walk_back_clip;
    if (!a.strafe_left_clip.empty()) j["strafe_left_clip"] = a.strafe_left_clip;
    if (!a.strafe_right_clip.empty()) j["strafe_right_clip"] = a.strafe_right_clip;
    if (!a.death_clip.empty()) j["death_clip"] = a.death_clip;
    if (!a.knockdown_clip.empty()) j["knockdown_clip"] = a.knockdown_clip;
    if (!a.flinch_front_clip.empty()) j["flinch_front_clip"] = a.flinch_front_clip;
    if (!a.flinch_back_clip.empty()) j["flinch_back_clip"] = a.flinch_back_clip;
    if (!a.flinch_left_clip.empty()) j["flinch_left_clip"] = a.flinch_left_clip;
    if (!a.flinch_right_clip.empty()) j["flinch_right_clip"] = a.flinch_right_clip;
    if (!a.hit_react_medium_clip.empty()) j["hit_react_medium_clip"] = a.hit_react_medium_clip;
    if (!a.hit_react_heavy_clip.empty()) j["hit_react_heavy_clip"] = a.hit_react_heavy_clip;
    if (!a.run_clip.empty()) j["run_clip"] = a.run_clip;
    if (a.chase_speed > 0.0f) j["chase_speed"] = a.chase_speed;
    if (a.disable_circle_strafe) j["disable_circle_strafe"] = true;
    if (a.max_hp_override > 0) j["max_hp_override"] = a.max_hp_override;
    if (a.max_poise_override > 0.0f) j["max_poise_override"] = a.max_poise_override;
    if (a.scripted_death_seconds > 0.0f)
        j["scripted_death_seconds"] = a.scripted_death_seconds;
    if (!a.scripted_death_pain_clip.empty())
        j["scripted_death_pain_clip"] = a.scripted_death_pain_clip;
    if (a.scripted_death_drain_to_fraction > 0.0f)
        j["scripted_death_drain_to_fraction"] = a.scripted_death_drain_to_fraction;
    if (a.scripted_death_drain_exponent != 1.0f)
        j["scripted_death_drain_exponent"] = a.scripted_death_drain_exponent;
    // Boss fields — only emit non-defaults so non-boss archetypes
    // serialize compactly. Round-trip preserves them regardless.
    if (a.is_boss) j["is_boss"] = true;
    if (!a.boss_name.empty()) j["boss_name"] = a.boss_name;
    if (!a.encounter_audio_bed.empty()) j["encounter_audio_bed"] = a.encounter_audio_bed;
    if (!a.felled_message.empty()) j["felled_message"] = a.felled_message;
    if (!a.felled_flag.empty()) j["felled_flag"] = a.felled_flag;
    if (!a.show_felled_overlay) j["show_felled_overlay"] = false;
    if (!a.initial_state.empty()) j["initial_state"] = a.initial_state;
    if (!a.engage_clip.empty()) j["engage_clip"] = a.engage_clip;
    if (a.initial_freeze_at_seconds > 0.0f)
        j["initial_freeze_at_seconds"] = a.initial_freeze_at_seconds;
}

void from_json(const nlohmann::json& j, EnemyArchetype& a)
{
    j.at("id").get_to(a.id);
    a.actions = j.value("actions", std::vector<EnemyAction>{});
    a.tree_id = j.value("tree", std::string("humanoid_basic"));
    if (j.contains("vision_fov_degrees"))
        a.vision_fov_degrees = j.at("vision_fov_degrees").get<float>();
    if (j.contains("vision_range_meters"))
        a.vision_range_meters = j.at("vision_range_meters").get<float>();
    a.faction = parseFaction(j.value("faction", std::string("Hostile")));
    a.is_npc = j.value("is_npc", false);
    a.form = parseForm(j.value("form", std::string("DamnedSoul")));
    a.skeleton_id = j.value("skeleton_id", std::string("player"));
    a.idle_clip = j.value("idle_clip", std::string{});
    a.combat_idle_clip = j.value("combat_idle_clip", std::string{});
    a.walk_clip = j.value("walk_clip", std::string{});
    a.walk_back_clip = j.value("walk_back_clip", std::string{});
    a.strafe_left_clip = j.value("strafe_left_clip", std::string{});
    a.strafe_right_clip = j.value("strafe_right_clip", std::string{});
    a.death_clip = j.value("death_clip", std::string{});
    a.knockdown_clip = j.value("knockdown_clip", std::string{});
    a.flinch_front_clip = j.value("flinch_front_clip", std::string{});
    a.flinch_back_clip = j.value("flinch_back_clip", std::string{});
    a.flinch_left_clip = j.value("flinch_left_clip", std::string{});
    a.flinch_right_clip = j.value("flinch_right_clip", std::string{});
    a.hit_react_medium_clip = j.value("hit_react_medium_clip", std::string{});
    a.hit_react_heavy_clip = j.value("hit_react_heavy_clip", std::string{});
    a.run_clip = j.value("run_clip", std::string{});
    a.chase_speed = j.value("chase_speed", 0.0f);
    a.disable_circle_strafe = j.value("disable_circle_strafe", false);
    a.max_hp_override = j.value("max_hp_override", 0);
    a.max_poise_override = j.value("max_poise_override", 0.0f);
    a.scripted_death_seconds = j.value("scripted_death_seconds", 0.0f);
    a.scripted_death_pain_clip = j.value("scripted_death_pain_clip", std::string{});
    a.scripted_death_drain_to_fraction = j.value("scripted_death_drain_to_fraction", 0.0f);
    a.scripted_death_drain_exponent = j.value("scripted_death_drain_exponent", 1.0f);
    // Boss fields. All default to false / empty -- non-boss archetypes
    // (limbo_shade, etc.) leave them all unset and carry no boss
    // semantics. See boss_backend.md sections 1-12.
    a.is_boss = j.value("is_boss", false);
    a.boss_name = j.value("boss_name", std::string{});
    a.encounter_audio_bed = j.value("encounter_audio_bed", std::string{});
    a.felled_message = j.value("felled_message", std::string{});
    a.felled_flag = j.value("felled_flag", std::string{});
    a.show_felled_overlay = j.value("show_felled_overlay", true);
    a.initial_state = j.value("initial_state", std::string{});
    a.engage_clip = j.value("engage_clip", std::string{});
    a.initial_freeze_at_seconds = j.value("initial_freeze_at_seconds", 0.0f);
    // Field name is "hurtbox_decls" to match the C++ field and the
    // wolf.json (every consumer reads the same name). The deserializer
    // previously looked for "hurtboxes" which silently no-op'd on the
    // wolf -- archetype.hurtbox_decls came out empty, spawn fell
    // through to the player-rig hurtboxes (mixamorig:* joint names
    // that don't exist on the wolf skeleton -> all hurtboxes collapsed
    // to actor origin at the wolf's feet -> player swings missed every
    // time).
    if (j.contains("hurtbox_decls") && j.at("hurtbox_decls").is_array())
    {
        for (const auto& h : j.at("hurtbox_decls"))
        {
            selva::combat::HurtboxDecl d;
            d.joint_a = h.value("joint_a", std::string{});
            d.joint_b = h.value("joint_b", std::string{});
            d.region = parseHurtRegion(h.value("region", std::string("Torso")));
            d.radius_scale = h.value("radius_scale", 0.5f);
            d.damage_multiplier = h.value("damage_multiplier", 1.0f);
            a.hurtbox_decls.push_back(std::move(d));
        }
    }
    if (j.contains("lockon_points") && j.at("lockon_points").is_array())
    {
        for (const auto& p : j.at("lockon_points"))
        {
            selva::anim::LockOnPointDecl d;
            d.id = p.value("id", std::string{});
            d.joint = p.value("joint", std::string{});
            d.is_default = p.value("default", false);
            if (!d.joint.empty())
                a.lockon_points.push_back(std::move(d));
        }
    }
}

void EnemyArchetypeRegistry::loadDirectory(const std::filesystem::path& dir)
{
    if (!std::filesystem::exists(dir) || !std::filesystem::is_directory(dir))
    {
        selva::combat::combatLog("[archetype] directory not found: {}", dir.string());
        return;
    }
    for (const auto& entry : std::filesystem::directory_iterator(dir))
    {
        if (!entry.is_regular_file())
            continue;
        if (entry.path().extension() != ".json")
            continue;
        std::ifstream in(entry.path());
        if (!in)
        {
            selva::combat::combatLog("[archetype] cannot open {}", entry.path().string());
            continue;
        }
        try
        {
            nlohmann::json j;
            in >> j;
            EnemyArchetype arch = j.get<EnemyArchetype>();
            if (arch.id.empty())
            {
                selva::combat::combatLog("[archetype] {} has empty id; skipping",
                                         entry.path().string());
                continue;
            }
            const std::string id = arch.id;
            by_id[id] = std::move(arch);
            selva::combat::combatLog("[archetype] loaded '{}' ({} actions) from {}", id,
                                     by_id[id].actions.size(), entry.path().string());
        }
        catch (const std::exception& e)
        {
            selva::combat::combatLog("[archetype] parse error in {}: {}", entry.path().string(),
                                     e.what());
        }
    }
}

const EnemyArchetype* EnemyArchetypeRegistry::get(const std::string& id) const
{
    const auto it = by_id.find(id);
    return (it == by_id.end()) ? nullptr : &it->second;
}

EnemyArchetypeRegistry& archetypes()
{
    static EnemyArchetypeRegistry instance;
    return instance;
}

} // namespace selva::gameplay
