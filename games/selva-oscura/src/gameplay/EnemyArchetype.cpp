#include "gameplay/EnemyArchetype.h"

#include "anim/ClipGeometry.h"
#include "anim/ClipRegistry.h"
#include "anim/SkeletalAssets.h"
#include "anim/SkeletonJointMap.h"
#include "combat/CombatLog.h"

#include <nlohmann/json.hpp>

#include <algorithm>
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
         {"effective_reach_override", a.effective_reach_override},
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
         {"playback_rate", a.playback_rate},
         {"cancel_fraction", a.cancel_fraction},
         {"locks_movement", a.locks_movement}};
}

void from_json(const nlohmann::json& j, EnemyAction& a)
{
    j.at("id").get_to(a.id);
    j.at("clip").get_to(a.clip);
    a.range_min = j.value("range_min", 0.0f);
    // effective_reach_override is the new (post-Phase-4b) authoring
    // key. range_max is the legacy key (semantically the same number)
    // -- read it as a back-compat fallback so existing archetypes
    // (shade, wolf) keep working until their JSON is migrated.
    a.effective_reach_override = j.value("effective_reach_override", j.value("range_max", 0.0f));
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
    a.playback_rate = j.value("playback_rate", 1.0f);
    if (a.playback_rate <= 0.0f)
        a.playback_rate = 1.0f;
    a.cancel_fraction = j.value("cancel_fraction", 1.0f);
    a.locks_movement = j.value("locks_movement", true);
}

namespace
{
// Each archetype field gets a "skip emit if at default" check. The
// rules are stable across fields so we group them into one helper
// per archetype-section to keep to_json itself a flat call list.
void emitClipFields(nlohmann::json& j, const EnemyArchetype& a)
{
    auto emit = [&](const char* key, const std::string& v)
    {
        if (!v.empty())
            j[key] = v;
    };
    emit("idle_clip", a.idle_clip);
    emit("combat_idle_clip", a.combat_idle_clip);
    emit("walk_clip", a.walk_clip);
    emit("walk_back_clip", a.walk_back_clip);
    emit("strafe_left_clip", a.strafe_left_clip);
    emit("strafe_right_clip", a.strafe_right_clip);
    emit("death_clip", a.death_clip);
    emit("knockdown_clip", a.knockdown_clip);
    emit("flinch_front_clip", a.flinch_front_clip);
    emit("flinch_back_clip", a.flinch_back_clip);
    emit("flinch_left_clip", a.flinch_left_clip);
    emit("flinch_right_clip", a.flinch_right_clip);
    emit("hit_react_medium_clip", a.hit_react_medium_clip);
    emit("hit_react_heavy_clip", a.hit_react_heavy_clip);
    emit("run_clip", a.run_clip);
    emit("aggro_clip", a.aggro_clip);
    emit("spawn_clip", a.spawn_clip);
}

void emitCombatFields(nlohmann::json& j, const EnemyArchetype& a)
{
    if (a.chase_speed > 0.0f)
        j["chase_speed"] = a.chase_speed;
    if (a.disable_circle_strafe)
        j["disable_circle_strafe"] = true;
    if (a.max_hp_override > 0)
        j["max_hp_override"] = a.max_hp_override;
    if (a.max_poise_override > 0.0f)
        j["max_poise_override"] = a.max_poise_override;
    if (a.scripted_death_seconds > 0.0f)
        j["scripted_death_seconds"] = a.scripted_death_seconds;
    if (!a.scripted_death_pain_clip.empty())
        j["scripted_death_pain_clip"] = a.scripted_death_pain_clip;
    if (a.scripted_death_drain_to_fraction > 0.0f)
        j["scripted_death_drain_to_fraction"] = a.scripted_death_drain_to_fraction;
    if (a.scripted_death_drain_exponent != 1.0f)
        j["scripted_death_drain_exponent"] = a.scripted_death_drain_exponent;
    if (a.sangue_drop != 0u)
        j["sangue_drop"] = a.sangue_drop;
    if (!a.loot_drops.empty())
    {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& d : a.loot_drops)
        {
            nlohmann::json entry;
            entry["config_path"] = d.config_path;
            if (d.min_qty != 1)
                entry["min"] = d.min_qty;
            if (d.max_qty != 1)
                entry["max"] = d.max_qty;
            if (d.base_chance != 1.0f)
                entry["chance"] = d.base_chance;
            arr.push_back(std::move(entry));
        }
        j["loot_drops"] = std::move(arr);
    }
}

void emitBossFields(nlohmann::json& j, const EnemyArchetype& a)
{
    if (a.is_boss)
        j["is_boss"] = true;
    if (!a.boss_name.empty())
        j["boss_name"] = a.boss_name;
    if (!a.encounter_audio_bed.empty())
        j["encounter_audio_bed"] = a.encounter_audio_bed;
    if (!a.felled_message.empty())
        j["felled_message"] = a.felled_message;
    if (!a.felled_flag.empty())
        j["felled_flag"] = a.felled_flag;
    if (!a.show_felled_overlay)
        j["show_felled_overlay"] = false;
    if (!a.initial_state.empty())
        j["initial_state"] = a.initial_state;
    if (!a.engage_clip.empty())
        j["engage_clip"] = a.engage_clip;
    if (a.initial_freeze_at_seconds > 0.0f)
        j["initial_freeze_at_seconds"] = a.initial_freeze_at_seconds;
    if (a.spawn_clip_freeze_at_seconds > 0.0f)
        j["spawn_clip_freeze_at_seconds"] = a.spawn_clip_freeze_at_seconds;
}
} // namespace

void to_json(nlohmann::json& j, const EnemyArchetype& a)
{
    j = nlohmann::json{{"id", a.id}, {"actions", a.actions}, {"tree", a.tree_id}};
    if (a.vision_fov_degrees.has_value())
        j["vision_fov_degrees"] = *a.vision_fov_degrees;
    if (a.vision_range_meters.has_value())
        j["vision_range_meters"] = *a.vision_range_meters;
    // Each "skip if default" rule is documented in the per-section
    // helpers below; the chain is grouped to keep to_json a flat call
    // list (lizard would otherwise fail on its cyclomatic complexity).
    if (a.faction != Faction::Hostile)
        j["faction"] = factionName(a.faction);
    if (a.is_npc)
        j["is_npc"] = true;
    if (!a.display_name.empty())
        j["display_name"] = a.display_name;
    if (!a.examine_text.empty())
        j["examine_text"] = a.examine_text;
    if (a.interact_range_meters > 0.0f)
        j["interact_range_meters"] = a.interact_range_meters;
    if (!a.talk_requires_flag.empty())
        j["talk_requires_flag"] = a.talk_requires_flag;
    if (a.form != Form::DamnedSoul)
        j["form"] = formName(a.form);
    if (!a.skeleton_id.empty() && a.skeleton_id != "humanoid_male")
        j["skeleton_id"] = a.skeleton_id;
    if (!a.mesh_path.empty())
        j["mesh_path"] = a.mesh_path;
    if (!a.mesh_path_variants.empty())
        j["mesh_path_variants"] = a.mesh_path_variants;
    if (a.random_face_morphs)
        j["random_face_morphs"] = a.random_face_morphs;
    if (!a.character_path.empty())
        j["character_path"] = a.character_path;
    emitClipFields(j, a);
    emitCombatFields(j, a);
    emitBossFields(j, a);
}

namespace
{
void loadClipFields(const nlohmann::json& j, EnemyArchetype& a)
{
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
    a.aggro_clip = j.value("aggro_clip", std::string{});
    a.spawn_clip = j.value("spawn_clip", std::string{});
}

void loadCombatFields(const nlohmann::json& j, EnemyArchetype& a)
{
    a.chase_speed = j.value("chase_speed", 0.0f);
    a.disable_circle_strafe = j.value("disable_circle_strafe", false);
    a.max_hp_override = j.value("max_hp_override", 0);
    a.max_poise_override = j.value("max_poise_override", 0.0f);
    a.scripted_death_seconds = j.value("scripted_death_seconds", 0.0f);
    a.scripted_death_pain_clip = j.value("scripted_death_pain_clip", std::string{});
    a.scripted_death_drain_to_fraction = j.value("scripted_death_drain_to_fraction", 0.0f);
    a.scripted_death_drain_exponent = j.value("scripted_death_drain_exponent", 1.0f);
    a.sangue_drop = j.value("sangue_drop", std::uint32_t{0});
    if (j.contains("loot_drops") && j["loot_drops"].is_array())
    {
        for (const auto& entry : j["loot_drops"])
        {
            if (!entry.is_object() || !entry.contains("config_path"))
                continue;
            engine::ecs::DropEntry d;
            d.config_path = entry.value("config_path", std::string{});
            d.min_qty = entry.value("min", 1);
            d.max_qty = entry.value("max", 1);
            d.base_chance = entry.value("chance", 1.0f);
            if (d.max_qty < d.min_qty)
                d.max_qty = d.min_qty;
            a.loot_drops.push_back(std::move(d));
        }
    }
}

void loadBossFields(const nlohmann::json& j, EnemyArchetype& a)
{
    // All default to false / empty -- non-boss archetypes leave them
    // unset and carry no boss semantics. See boss_backend.md 1-12.
    a.is_boss = j.value("is_boss", false);
    a.boss_name = j.value("boss_name", std::string{});
    a.boss_name_key = j.value("boss_name_key", std::string{});
    a.encounter_audio_bed = j.value("encounter_audio_bed", std::string{});
    a.felled_message = j.value("felled_message", std::string{});
    a.felled_message_key = j.value("felled_message_key", std::string{});
    a.felled_flag = j.value("felled_flag", std::string{});
    a.show_felled_overlay = j.value("show_felled_overlay", true);
    a.initial_state = j.value("initial_state", std::string{});
    a.engage_clip = j.value("engage_clip", std::string{});
    a.initial_freeze_at_seconds = j.value("initial_freeze_at_seconds", 0.0f);
    a.spawn_clip_freeze_at_seconds = j.value("spawn_clip_freeze_at_seconds", 0.0f);
}

void loadHurtboxDecls(const nlohmann::json& j, EnemyArchetype& a)
{
    // The deserializer used to read "hurtboxes" instead of
    // "hurtbox_decls" which silently no-op'd on the wolf -- archetype
    // came out with no hurtboxes, spawn fell back to player-rig joint
    // names that don't exist on wolf, all hurtboxes collapsed to actor
    // origin at feet, every player swing missed.
    if (!j.contains("hurtbox_decls") || !j.at("hurtbox_decls").is_array())
        return;
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

void loadLockOnPoints(const nlohmann::json& j, EnemyArchetype& a)
{
    if (!j.contains("lockon_points") || !j.at("lockon_points").is_array())
        return;
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

void loadTintAndDecls(const nlohmann::json& j, EnemyArchetype& a)
{
    // Color lives on the AuthoredCharacter file referenced by
    // character_path, not on the archetype. If tint_color shows up
    // in an archetype JSON it's an old file; move the color into the
    // character file and delete the archetype key.
    a.transform_target_archetype =
        j.value("transform_target_archetype",
                j.value("tint_burn_target_archetype", std::string{})); // accept legacy alias
    loadHurtboxDecls(j, a);
    a.disable_hurtboxes = j.value("disable_hurtboxes", false);
    a.intangible = j.value("intangible", false);
    if (j.contains("avoids_hazards") && j.at("avoids_hazards").is_array())
        for (const auto& s : j.at("avoids_hazards"))
            a.avoids_hazards.push_back(s.get<std::string>());
    loadLockOnPoints(j, a);
}
} // namespace

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
    a.display_name = j.value("display_name", std::string{});
    a.display_name_key = j.value("display_name_key", std::string{});
    a.examine_label_key = j.value("examine_label_key", std::string{});
    a.examine_text_key = j.value("examine_text_key", std::string{});
    a.examine_text = j.value("examine_text", std::string{});
    a.interact_range_meters = j.value("interact_range_meters", 0.0f);
    a.talk_requires_flag = j.value("talk_requires_flag", std::string{});
    a.face_player_range_meters = j.value("face_player_range_meters", 0.0f);
    a.acknowledgment_max_angle_radians = j.value("acknowledgment_max_angle_radians", 0.6f);
    a.acknowledgment_turn_rate_scale = j.value("acknowledgment_turn_rate_scale", 0.15f);
    a.form = parseForm(j.value("form", std::string("DamnedSoul")));
    a.skeleton_id = j.value("skeleton_id", std::string("humanoid_male"));
    a.mesh_path = j.value("mesh_path", std::string{});
    a.mesh_path_variants = j.value("mesh_path_variants", std::vector<std::string>{});
    a.random_face_morphs = j.value("random_face_morphs", false);
    // Accept legacy "appearance_path" JSON key as a fallback for
    // archetype files written before the character_path rename. The
    // authored file on disk changed shape (Appearance -> AuthoredChar)
    // but the referencing archetype's key was renamed at the same
    // time; both spellings resolve to the same on-disk file.
    a.character_path = j.value("character_path", j.value("appearance_path", std::string{}));
    loadClipFields(j, a);
    loadCombatFields(j, a);
    loadBossFields(j, a);
    loadTintAndDecls(j, a);
}

void EnemyArchetypeRegistry::loadDirectory(const std::filesystem::path& dir)
{
    if (!std::filesystem::exists(dir) || !std::filesystem::is_directory(dir))
    {
        selva::combat::combatLog("[archetype] directory not found: {}", dir.string());
        return;
    }
    // Pass 1: load raw JSON for each file. Defer deserialization so the
    // inheritance resolver can merge parent fields BEFORE the
    // EnemyArchetype struct gets built. Order of files on disk is
    // arbitrary; we can't deserialize-then-merge because the struct
    // fields lose the "was-this-key-set-by-child" signal once the JSON
    // is parsed (defaults are indistinguishable from absent keys).
    std::unordered_map<std::string, nlohmann::json> raw_by_id;
    std::unordered_map<std::string, std::filesystem::path> path_by_id;
    for (const auto& entry : std::filesystem::directory_iterator(dir))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".json")
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
            const std::string id = j.value("id", std::string{});
            if (id.empty())
            {
                selva::combat::combatLog("[archetype] {} has empty id; skipping",
                                         entry.path().string());
                continue;
            }
            raw_by_id[id] = std::move(j);
            path_by_id[id] = entry.path();
        }
        catch (const std::exception& e)
        {
            selva::combat::combatLog("[archetype] parse error in {}: {}", entry.path().string(),
                                     e.what());
        }
    }
    // Pass 2: deserialize. If "inherits" is present, walk the chain
    // (depth-limited to catch cycles), merge each ancestor's JSON
    // beneath the child via merge_patch semantics: child object keys
    // override parent, child arrays replace parent arrays wholesale.
    // For our schema this gives the right behavior -- inheriting
    // larva_aged_feeder from larva_aged shares the actions[] array,
    // tints, hazard list, etc. unless the child explicitly re-declares
    // them.
    constexpr int kMaxInheritDepth = 8;
    for (auto& [id, raw] : raw_by_id)
    {
        try
        {
            nlohmann::json merged = raw;
            std::string cursor = raw.value("inherits", std::string{});
            int depth = 0;
            while (!cursor.empty())
            {
                if (++depth > kMaxInheritDepth)
                {
                    selva::combat::combatLog(
                        "[archetype] '{}' inherits chain exceeded depth {}; aborting merge", id,
                        kMaxInheritDepth);
                    break;
                }
                auto parent_it = raw_by_id.find(cursor);
                if (parent_it == raw_by_id.end())
                {
                    selva::combat::combatLog(
                        "[archetype] '{}' inherits unknown parent '{}'; skipping merge", id,
                        cursor);
                    break;
                }
                // Splice the current merged JSON on top of the parent so
                // child values win. Reset to a fresh copy of parent first,
                // then merge_patch the child overrides in.
                nlohmann::json next = parent_it->second;
                next.merge_patch(merged);
                merged = std::move(next);
                cursor = parent_it->second.value("inherits", std::string{});
            }
            // Strip the "inherits" key before deserialization -- it's a
            // load-time directive, not a struct field.
            merged.erase("inherits");
            // Override id with the child's id (merge_patch would have
            // kept it correct, but the parent's id would clobber if a
            // template used the same field name; defensive).
            merged["id"] = id;
            EnemyArchetype arch = merged.get<EnemyArchetype>();
            by_id[id] = std::move(arch);
            selva::combat::combatLog("[archetype] loaded '{}' ({} actions) from {}", id,
                                     by_id[id].actions.size(), path_by_id[id].string());
        }
        catch (const std::exception& e)
        {
            selva::combat::combatLog("[archetype] deserialize error in '{}': {}", id, e.what());
        }
    }
}

void EnemyArchetypeRegistry::resolveAllActionReach()
{
    for (auto& [arch_id, arch] : by_id)
    {
        const selva::anim::Skeleton& skel = selva::anim::skeletonByKey(arch.skeleton_id);
        const selva::anim::ClipRegistry& clips = selva::anim::clipsByKey(arch.skeleton_id);
        const selva::anim::SkeletonJointMap& jmap = selva::anim::jointMapByKey(arch.skeleton_id);
        const std::string& hips = jmap.hips;
        for (auto& action : arch.actions)
        {
            action.resolved_effective_reach = 0.0f;
            // Actions without a hitbox can't have a reach. Their
            // gating falls through to effective_reach_override or
            // the tunable default downstream.
            if (action.hitbox_joint.empty())
                continue;
            const selva::anim::AnimationClip* clip = clips.get(action.clip);
            if (clip == nullptr || !clip->isLoaded())
            {
                selva::combat::combatLog("[reach] {}.{} clip '{}' missing -- reach=0", arch_id,
                                         action.id, action.clip);
                continue;
            }
            const float dur = clip->duration();
            float win_start = 0.0f;
            float win_end = dur;
            if (action.windup_seconds > 0.0f || action.active_seconds > 0.0f)
            {
                win_start = action.windup_seconds;
                win_end = std::min(action.windup_seconds + action.active_seconds, dur);
            }
            const selva::anim::JointReachResult scan = selva::anim::computeJointReach(
                *clip, skel, hips.c_str(), action.hitbox_joint.c_str(), win_start, win_end);
            const float joint_extension = scan.xz_max_meters;
            const float tip = std::abs(action.hitbox_tip_offset_z);
            action.resolved_effective_reach = joint_extension + action.hitbox_radius + tip;
            selva::combat::combatLog("[reach] {}.{} clip='{}' joint='{}' window=[{:.3f},{:.3f}]s "
                                     "joint_extension={:.3f}m hitbox_radius={:.2f}m tip={:.2f}m "
                                     "-> reach={:.3f}m (samples={})",
                                     arch_id, action.id, action.clip, action.hitbox_joint,
                                     win_start, win_end, joint_extension, action.hitbox_radius, tip,
                                     action.resolved_effective_reach, scan.sample_count);
            // Sanity-check: most legitimate strikes have reach >= 0.5m.
            // Below 0.3m almost always means the hitbox_joint was
            // picked from cosmology rather than clip authoring (e.g.
            // hitbox_joint=Head on a hand-swing clip). Loud warning
            // so the bug is caught at boot, not in playtest. Per
            // [[feedback_hitbox_joint_must_match_visible_strike]].
            if (action.resolved_effective_reach > 0.0f && action.resolved_effective_reach < 0.30f)
            {
                selva::combat::combatLog(
                    "[reach:WARN] {}.{} reach={:.3f}m is suspiciously small. "
                    "Verify hitbox_joint='{}' is the joint that visibly STRIKES "
                    "in clip '{}' (not the joint cosmology suggests). See "
                    "memory/feedback_hitbox_joint_must_match_visible_strike.md",
                    arch_id, action.id, action.resolved_effective_reach, action.hitbox_joint,
                    action.clip);
            }
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
