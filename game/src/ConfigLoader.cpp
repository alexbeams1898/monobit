#include "ConfigLoader.h"

#include "ecs/Components.h"
#include "ecs/GameComponents.h"
#include "ecs/GameConfig.h"

#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <nlohmann/json.hpp>
#include <unordered_map>

using json = nlohmann::json;

using LoaderFn = std::function<void(EntityManager&, entt::entity, const json&)>;

static void loadTransform(EntityManager& em, entt::entity entity, const json& j)
{
    Transform t;
    t.x = j.value("x", 0.0f);
    t.y = j.value("y", 0.0f);
    t.rotation = j.value("rotation", 0.0f);
    t.scale = j.value("scale", 1.0f);
    em.registry().emplace<Transform>(entity, t);
}

static void loadVelocity(EntityManager& em, entt::entity entity, const json& j)
{
    Velocity v;
    v.dx = j.value("dx", 0.0f);
    v.dy = j.value("dy", 0.0f);
    em.registry().emplace<Velocity>(entity, v);
}

static void loadHealth(EntityManager& em, entt::entity entity, const json& j)
{
    Health h;
    h.current = j.value("current", 0);
    h.max = j.value("max", 0);
    em.registry().emplace<Health>(entity, h);
}

static void loadSprite(EntityManager& em, entt::entity entity, const json& j)
{
    auto& s = em.registry().get_or_emplace<Sprite>(entity);
    s.texture_path = j.value("texture_path", s.texture_path);
    s.src_x = j.value("src_x", s.src_x);
    s.src_y = j.value("src_y", s.src_y);
    s.src_w = j.value("src_w", s.src_w);
    s.src_h = j.value("src_h", s.src_h);
    s.layer = j.value("layer", s.layer);
}

static void loadCollider(EntityManager& em, entt::entity entity, const json& j)
{
    Collider c;
    c.width = j.value("width", 0.0f);
    c.height = j.value("height", 0.0f);
    c.is_solid = j.value("is_solid", true);
    em.registry().emplace<Collider>(entity, c);
}

static void loadStats(EntityManager& em, entt::entity entity, const json& j)
{
    Stats s;
    s.str = j.value("str", 1);
    s.dex = j.value("dex", 1);
    s.end = j.value("end", 1);
    s.lck = j.value("lck", 1);
    em.registry().emplace<Stats>(entity, s);
}

static void loadBody(EntityManager& em, entt::entity entity, const json& j)
{
    Body b;
    b.base_hp = j.value("base_hp", 0);
    b.base_defense = j.value("base_defense", 0);
    b.unarmed_damage = j.value("unarmed_damage", b.unarmed_damage);
    b.unarmed_weight = j.value("unarmed_weight", b.unarmed_weight);
    b.unarmed_str_scaling = j.value("unarmed_str_scaling", b.unarmed_str_scaling);
    b.unarmed_dex_scaling = j.value("unarmed_dex_scaling", b.unarmed_dex_scaling);
    em.registry().emplace<Body>(entity, b);
}

static void loadExperience(EntityManager& em, entt::entity entity, const json& j)
{
    Experience e;
    e.current_xp = j.value("current_xp", 0);
    e.xp_to_next = j.value("xp_to_next", 100);
    e.level = j.value("level", 1);
    e.stat_points = j.value("stat_points", 0);
    em.registry().emplace<Experience>(entity, e);
}

static void loadWeapon(EntityManager& em, entt::entity entity, const json& j)
{
    Weapon w;
    w.name = j.value("name", std::string{});
    w.weight = j.value("weight", 0.5f);
    w.str_scaling = j.value("str_scaling", 0.25f);
    w.dex_scaling = j.value("dex_scaling", 0.25f);
    w.str_requirement = j.value("str_requirement", 0);
    w.dex_requirement = j.value("dex_requirement", 0);
    w.base_damage = j.value("base_damage", 5.0f);
    em.registry().emplace<Weapon>(entity, w);
}

static void loadFacingDirection(EntityManager& em, entt::entity entity, const json& /*j*/)
{
    em.registry().emplace<FacingDirection>(entity);
}

static void loadAutoAttackMode(EntityManager& em, entt::entity entity, const json& j)
{
    AutoAttackMode a;
    a.enabled = j.value("enabled", false);
    em.registry().emplace<AutoAttackMode>(entity, a);
}

static void loadShield(EntityManager& em, entt::entity entity, const json& j)
{
    Shield s;
    s.max_guard = j.value("max_guard", 100.0f);
    s.guard_health = s.max_guard;
    em.registry().emplace<Shield>(entity, s);
}

static void loadSolidColor(EntityManager& em, entt::entity entity, const json& j)
{
    SolidColor c;
    c.r = j.value("r", 1.0f);
    c.g = j.value("g", 1.0f);
    c.b = j.value("b", 1.0f);
    em.registry().emplace<SolidColor>(entity, c);
}

static void loadRestSpot(EntityManager& em, entt::entity entity, const json& j)
{
    RestSpot r;
    r.radius = j.value("radius", 64.0f);
    em.registry().emplace<RestSpot>(entity, r);
}

static void loadPoise(EntityManager& em, entt::entity entity, const json& j)
{
    Poise p;
    p.max = j.value("max", 0.0f);
    em.registry().emplace<Poise>(entity, p);
}

static void loadLoot(EntityManager& em, entt::entity entity, const json& j)
{
    Loot l;
    l.xp_drop = j.value("xp_drop", 20);
    if (j.contains("drops") && j["drops"].is_array())
    {
        for (const auto& dj : j["drops"])
        {
            DropEntry d;
            d.config_path = dj.value("item", std::string{});
            d.min_qty = dj.value("min", 1);
            d.max_qty = dj.value("max", 1);
            d.base_chance = dj.value("chance", 1.0f);
            l.drops.push_back(std::move(d));
        }
    }
    em.registry().emplace<Loot>(entity, std::move(l));
}

static bool emplaceAnimationFromSheet(EntityManager& em, entt::entity entity,
                                      const std::string& sheetPath)
{
    if (sheetPath.empty())
        return false;

    std::ifstream sheetFile(sheetPath);
    if (!sheetFile.is_open())
    {
        std::cerr << "[ConfigLoader] Cannot open animation sheet: " << sheetPath << "\n";
        return false;
    }

    json sheetData;
    try
    {
        sheetFile >> sheetData;
    }
    catch (const std::exception& e)
    {
        std::cerr << "[ConfigLoader] Error parsing animation sheet " << sheetPath << ": "
                  << e.what() << "\n";
        return false;
    }

    Animation anim;
    anim.frame_width = sheetData.value("frame_width", 32);
    anim.frame_height = sheetData.value("frame_height", 32);

    if (sheetData.contains("texture"))
    {
        auto& spr = em.registry().get_or_emplace<Sprite>(entity);
        spr.texture_path = sheetData.value("texture", spr.texture_path);
        spr.src_w = anim.frame_width;
        spr.src_h = anim.frame_height;
    }

    auto loadState = [&](const char* name, AnimState state)
    {
        if (sheetData.contains("states") && sheetData["states"].contains(name))
        {
            const auto& s = sheetData["states"][name];
            auto& sd = anim.states[static_cast<int>(state)];
            sd.row = s.value("row", 0);
            sd.frames = s.value("frames", 1);
            sd.duration = s.value("duration", 0.0f);
        }
    };

    loadState("idle", AnimState::Idle);
    loadState("walk", AnimState::Walk);
    loadState("attack", AnimState::Attack);
    loadState("hit", AnimState::Hit);
    loadState("death", AnimState::Death);

    int maxF = 1;
    for (const auto& state : anim.states)
        maxF = std::max(maxF, state.frames);
    anim.max_frames_per_state = maxF;

    em.registry().emplace<Animation>(entity, anim);
    return true;
}

static void loadAnimation(EntityManager& em, entt::entity entity, const json& j)
{
    const std::string sheetPath = j.value("sheet", std::string{});
    if (!emplaceAnimationFromSheet(em, entity, sheetPath))
        em.registry().emplace<Animation>(entity);
}

static void loadBodyParts(EntityManager& em, entt::entity parent, const json& j)
{
    const Transform* parentTransform = em.registry().try_get<Transform>(parent);

    for (const auto& part : j)
    {
        auto child = em.create();

        BodyPart bp;
        bp.parent = parent;
        bp.direction_from_facing = part.value("direction_from_facing", false);
        em.registry().emplace<BodyPart>(child, bp);

        Transform t;
        if (parentTransform)
        {
            t.x = parentTransform->x;
            t.y = parentTransform->y;
            t.scale = parentTransform->scale;
        }
        em.registry().emplace<Transform>(child, t);

        const std::string sheetPath = part.value("sheet", std::string{});
        emplaceAnimationFromSheet(em, child, sheetPath);

        if (em.registry().all_of<Sprite>(child))
            em.registry().get<Sprite>(child).layer = part.value("draw_order", 0);

        em.registry().emplace<Tag>(child, Tag{"body_part"});
    }
}

static void loadStamina(EntityManager& em, entt::entity entity, const json& /*j*/)
{
    // Max stamina is formula-derived from END in LevelingSystem::applyInitialDerivations.
    // Just emplace an empty component so the stamina system knows this entity uses stamina.
    em.registry().emplace<Stamina>(entity);
}

static void loadInventory(EntityManager& em, entt::entity entity, const json& j)
{
    Inventory inv;
    inv.max_slots = j.value("max_slots", 20);
    em.registry().emplace<Inventory>(entity, std::move(inv));
}

static void loadEquipment(EntityManager& em, entt::entity entity, const json& /*j*/)
{
    em.registry().emplace<Equipment>(entity);

    // Ensure entity always has a Weapon component (unarmed defaults) so there
    // is never a frame where CombatSystem sees no Weapon.
    // Body's natural weapon takes priority; FormulaConfig::fist is the fallback.
    if (!em.registry().all_of<Weapon>(entity))
    {
        Weapon w;
        w.name = "Fist";
        const Body* body = em.registry().try_get<Body>(entity);
        if (body != nullptr)
        {
            w.weight = body->unarmed_weight;
            w.base_damage = body->unarmed_damage;
            w.str_scaling = body->unarmed_str_scaling;
            w.dex_scaling = body->unarmed_dex_scaling;
        }
        else
        {
            const auto& f = em.registry().ctx().get<FormulaConfig>();
            w.weight = f.fist.weight;
            w.base_damage = f.fist.base_damage;
            w.str_scaling = f.fist.str_scaling;
            w.dex_scaling = f.fist.dex_scaling;
        }
        em.registry().emplace<Weapon>(entity, w);
    }
}

static void loadAIController(EntityManager& em, entt::entity entity, const json& j)
{
    AIController ai;
    ai.aggro_radius = j.value("aggro_radius", 0.0f);
    ai.turn_speed = j.value("turn_speed", 8.0f);
    ai.separation_strength = j.value("separation_strength", 1.0f);
    ai.arrival_radius = j.value("arrival_radius", 0.0f);
    ai.attack_radius = j.value("attack_radius", 0.0f);
    ai.tier = j.value("tier", 1);
    ai.sprint_multiplier = j.value("sprint_multiplier", 0.0f);
    ai.sprint_threshold = j.value("sprint_threshold", 0.0f);

    const std::string behavior = j.value("behavior", std::string{"idle"});
    if (behavior == "chase")
    {
        ai.state =
            (ai.aggro_radius > 0.0f) ? AIController::State::Idle : AIController::State::Chase;
    }
    else if (behavior == "attack")
        ai.state = AIController::State::Attack;

    em.registry().emplace<AIController>(entity, ai);

    NavAgent nav;
    nav.separation_strength = ai.separation_strength;
    em.registry().emplace<NavAgent>(entity, nav);
}

// clang-format off
static const std::unordered_map<std::string, LoaderFn> kComponentLoaders = {
    {"transform",        loadTransform},
    {"velocity",         loadVelocity},
    {"health",           loadHealth},
    {"sprite",           loadSprite},
    {"collider",         loadCollider},
    {"stats",            loadStats},
    {"experience",       loadExperience},
    {"weapon",           loadWeapon},
    {"facing",           loadFacingDirection},
    {"auto_attack_mode", loadAutoAttackMode},
    {"stamina",          loadStamina},
    {"shield",           loadShield},
    {"poise",            loadPoise},
    {"loot",             loadLoot},
    {"animation",        loadAnimation},
    {"ai_controller",    loadAIController},
    {"rest_spot",        loadRestSpot},
    {"solid_color",      loadSolidColor},
    {"body_parts",       loadBodyParts},
    {"inventory",        loadInventory},
    {"equipment",        loadEquipment},
    {"body",             loadBody},
};
// clang-format on

entt::entity ConfigLoader::loadEntity(EntityManager& em, const std::string& filePath)
{
    std::ifstream file(filePath);
    if (!file.is_open())
    {
        std::cerr << "[ConfigLoader] Cannot open: " << filePath << "\n";
        return entt::null;
    }

    json data;
    try
    {
        file >> data;
    }
    catch (const std::exception& e)
    {
        std::cerr << "[ConfigLoader] Error parsing " << filePath << ": " << e.what() << "\n";
        return entt::null;
    }

    auto entity = em.create();

    Tag tag;
    tag.name = data.value("tag", std::string{});
    em.registry().emplace<Tag>(entity, tag);

    if (!data.contains("components"))
        return entity;

    for (const auto& [key, value] : data["components"].items())
    {
        auto it = kComponentLoaders.find(key);
        if (it != kComponentLoaders.end())
            it->second(em, entity, value);
        else
            std::cerr << "[ConfigLoader] Unknown component key: \"" << key << "\" in " << filePath
                      << "\n";
    }

    return entity;
}

bool ConfigLoader::loadFormulas(EntityManager& em, const std::string& filePath)
{
    std::ifstream file(filePath);
    if (!file.is_open())
    {
        std::cerr << "[ConfigLoader] Cannot open formulas: " << filePath
                  << " -- using hardcoded defaults\n";
        return false;
    }

    json j;
    try
    {
        file >> j;
    }
    catch (const std::exception& e)
    {
        std::cerr << "[ConfigLoader] Error parsing " << filePath << ": " << e.what()
                  << " -- using hardcoded defaults\n";
        return false;
    }

    FormulaConfig& f = em.registry().ctx().get<FormulaConfig>();

    if (j.contains("hp"))
    {
        f.hp.base = j["hp"].value("base", f.hp.base);
        f.hp.scale = j["hp"].value("scale", f.hp.scale);
        f.hp.level_scale = j["hp"].value("level_scale", f.hp.level_scale);
    }
    if (j.contains("movement"))
    {
        f.movement.base = j["movement"].value("base", f.movement.base);
        f.movement.dex_scale = j["movement"].value("dex_scale", f.movement.dex_scale);
        f.movement.sprint_multiplier =
            j["movement"].value("sprint_multiplier", f.movement.sprint_multiplier);
        f.movement.sprint_blend = j["movement"].value("sprint_blend", f.movement.sprint_blend);
        f.movement.walk_blend = j["movement"].value("walk_blend", f.movement.walk_blend);
    }
    if (j.contains("carry_weight"))
    {
        f.carry_weight.str_scale = j["carry_weight"].value("str_scale", f.carry_weight.str_scale);
        f.carry_weight.end_scale = j["carry_weight"].value("end_scale", f.carry_weight.end_scale);
    }
    if (j.contains("defense"))
    {
        f.defense.str_scale = j["defense"].value("str_scale", f.defense.str_scale);
        f.defense.end_scale = j["defense"].value("end_scale", f.defense.end_scale);
        f.defense.level_scale = j["defense"].value("level_scale", f.defense.level_scale);
        f.defense.cap = j["defense"].value("cap", f.defense.cap);
    }
    if (j.contains("luck"))
    {
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
    if (j.contains("damage") && j["damage"].contains("grade_thresholds"))
    {
        const auto& gt = j["damage"]["grade_thresholds"];
        f.grade_thresholds.s = gt.value("S", f.grade_thresholds.s);
        f.grade_thresholds.a = gt.value("A", f.grade_thresholds.a);
        f.grade_thresholds.b = gt.value("B", f.grade_thresholds.b);
        f.grade_thresholds.c = gt.value("C", f.grade_thresholds.c);
        f.grade_thresholds.d = gt.value("D", f.grade_thresholds.d);
        f.grade_thresholds.e = gt.value("E", f.grade_thresholds.e);
    }
    if (j.contains("swing"))
    {
        f.swing.base_swing_time = j["swing"].value("base_swing_time", f.swing.base_swing_time);
        f.swing.weight_scale = j["swing"].value("weight_scale", f.swing.weight_scale);
        f.swing.stat_scale = j["swing"].value("stat_scale", f.swing.stat_scale);
        f.swing.two_handed_str_bonus =
            j["swing"].value("two_handed_str_bonus", f.swing.two_handed_str_bonus);
    }
    if (j.contains("stat_requirement"))
    {
        f.stat_requirement.penalty_rate =
            j["stat_requirement"].value("penalty_rate", f.stat_requirement.penalty_rate);
    }
    if (j.contains("leveling"))
    {
        f.leveling.xp_base = j["leveling"].value("xp_base", f.leveling.xp_base);
        f.leveling.xp_exponent = j["leveling"].value("xp_exponent", f.leveling.xp_exponent);
        f.leveling.xp_offset = j["leveling"].value("xp_offset", f.leveling.xp_offset);
        f.leveling.points_per_level =
            j["leveling"].value("points_per_level", f.leveling.points_per_level);
    }
    if (j.contains("poise"))
    {
        f.poise.end_scale = j["poise"].value("end_scale", f.poise.end_scale);
        f.poise.str_scale = j["poise"].value("str_scale", f.poise.str_scale);
        f.poise.weight_scale = j["poise"].value("weight_scale", f.poise.weight_scale);
        f.poise.stagger_duration = j["poise"].value("stagger_duration", f.poise.stagger_duration);
        f.poise.decay_window = j["poise"].value("decay_window", f.poise.decay_window);
    }

    if (j.contains("dodge"))
    {
        f.dodge.duration = j["dodge"].value("duration", f.dodge.duration);
        f.dodge.cooldown = j["dodge"].value("cooldown", f.dodge.cooldown);
    }

    if (j.contains("essence"))
    {
        f.essence.min = j["essence"].value("min", f.essence.min);
        f.essence.max = j["essence"].value("max", f.essence.max);
    }

    if (j.contains("xp_drop"))
    {
        f.xp_drop.log_scale = j["xp_drop"].value("log_scale", f.xp_drop.log_scale);
        f.xp_drop.min_fraction = j["xp_drop"].value("min_fraction", f.xp_drop.min_fraction);
        f.xp_drop.level_penalty = j["xp_drop"].value("level_penalty", f.xp_drop.level_penalty);
        f.xp_drop.essence_scale = j["xp_drop"].value("essence_scale", f.xp_drop.essence_scale);
    }

    if (j.contains("stamina"))
    {
        f.stamina.base_swing_cost =
            j["stamina"].value("base_swing_cost", f.stamina.base_swing_cost);
        f.stamina.swing_effort = j["stamina"].value("swing_effort", f.stamina.swing_effort);
        f.stamina.dodge_effort = j["stamina"].value("dodge_effort", f.stamina.dodge_effort);
        f.stamina.skill_effort = j["stamina"].value("skill_effort", f.stamina.skill_effort);
        f.stamina.sprint_effort = j["stamina"].value("sprint_effort", f.stamina.sprint_effort);
        f.stamina.sprint_dex_scale =
            j["stamina"].value("sprint_dex_scale", f.stamina.sprint_dex_scale);
        f.stamina.base = j["stamina"].value("base", f.stamina.base);
        f.stamina.end_scale = j["stamina"].value("end_scale", f.stamina.end_scale);
        f.stamina.recovery_rate = j["stamina"].value("recovery_rate", f.stamina.recovery_rate);
        f.stamina.recovery_delay = j["stamina"].value("recovery_delay", f.stamina.recovery_delay);
        f.stamina.exhaustion_stagger =
            j["stamina"].value("exhaustion_stagger", f.stamina.exhaustion_stagger);
    }

    if (j.contains("fist"))
    {
        f.fist.weight = j["fist"].value("weight", f.fist.weight);
        f.fist.base_damage = j["fist"].value("base_damage", f.fist.base_damage);
        f.fist.str_scaling = j["fist"].value("str_scaling", f.fist.str_scaling);
        f.fist.dex_scaling = j["fist"].value("dex_scaling", f.fist.dex_scaling);
    }

    f.loaded = true;
    std::cout << "[ConfigLoader] Loaded formulas from " << filePath << "\n";
    return true;
}

bool ConfigLoader::loadSounds(EntityManager& em, const std::string& filePath)
{
    std::ifstream file(filePath);
    if (!file.is_open())
    {
        std::cerr << "[ConfigLoader] Cannot open sounds: " << filePath
                  << " -- using hardcoded defaults\n";
        return false;
    }

    json j;
    try
    {
        file >> j;
    }
    catch (const std::exception& e)
    {
        std::cerr << "[ConfigLoader] Error parsing " << filePath << ": " << e.what()
                  << " -- using hardcoded defaults\n";
        return false;
    }

    SoundConfig& s = em.registry().ctx().get<SoundConfig>();

    auto load = [&](const char* key, SoundEntry& entry)
    {
        if (j.contains(key))
        {
            entry.path = j[key].value("path", entry.path);
            entry.volume = j[key].value("volume", entry.volume);
        }
    };

    load("player_attack", s.player_attack);
    load("player_skill", s.player_skill);
    load("player_dodge", s.player_dodge);
    load("hit", s.hit);
    load("parry", s.parry);
    load("death", s.death);
    load("pickup", s.pickup);
    load("level_up", s.level_up);
    load("stat_allocate", s.stat_allocate);
    load("wall_bump", s.wall_bump);
    load("footstep_walk", s.footstep_walk);
    load("footstep_run", s.footstep_run);
    load("rest_heal", s.rest_heal);
    load("game_over", s.game_over);
    load("low_stamina_heartbeat", s.low_stamina_heartbeat);

    s.loaded = true;
    std::cout << "[ConfigLoader] Loaded sounds from " << filePath << "\n";
    return true;
}

bool ConfigLoader::loadMusic(EntityManager& em, const std::string& filePath)
{
    std::ifstream file(filePath);
    if (!file.is_open())
    {
        std::cerr << "[ConfigLoader] Cannot open music config: " << filePath << "\n";
        return false;
    }

    json j;
    try
    {
        file >> j;
    }
    catch (const std::exception& e)
    {
        std::cerr << "[ConfigLoader] Error parsing " << filePath << ": " << e.what() << "\n";
        return false;
    }

    MusicConfig& mc = em.registry().ctx().get<MusicConfig>();
    mc.default_volume = j.value("default_volume", 0.6f);

    if (j.contains("tracks") && j["tracks"].is_array())
    {
        for (const auto& t : j["tracks"])
        {
            MusicConfig::Track track;
            track.path = t.value("path", "");
            track.volume = t.value("volume", mc.default_volume);
            if (!track.path.empty())
                mc.tracks.push_back(std::move(track));
        }
    }

    std::cout << "[ConfigLoader] Loaded " << mc.tracks.size() << " music tracks from " << filePath
              << "\n";
    return true;
}

bool ConfigLoader::loadWaves(EntityManager& em, const std::string& filePath)
{
    std::ifstream file(filePath);
    if (!file.is_open())
    {
        std::cerr << "[ConfigLoader] Cannot open waves: " << filePath << "\n";
        return false;
    }

    json j;
    try
    {
        file >> j;
    }
    catch (const std::exception& e)
    {
        std::cerr << "[ConfigLoader] Error parsing " << filePath << ": " << e.what() << "\n";
        return false;
    }

    WaveConfig& wc = em.registry().ctx().get<WaveConfig>();
    wc.spawn_near = j.value("spawn_near", wc.spawn_near);
    wc.spawn_far = j.value("spawn_far", wc.spawn_far);

    auto& gen = wc.gen;

    if (j.contains("enemies") && j["enemies"].is_array())
    {
        for (const auto& ej : j["enemies"])
        {
            WaveEnemyEntry entry;
            entry.config_path = ej.value("config", std::string{});
            entry.from_wave = ej.value("from_wave", 1);
            entry.weight = ej.value("weight", 1);
            gen.enemies.push_back(std::move(entry));
        }
    }

    gen.start_count = j.value("start_count", gen.start_count);
    gen.count_growth = j.value("count_growth", gen.count_growth);
    gen.max_count = j.value("max_count", gen.max_count);
    gen.start_interval = j.value("start_interval", gen.start_interval);
    gen.interval_decay = j.value("interval_decay", gen.interval_decay);
    gen.min_interval = j.value("min_interval", gen.min_interval);
    gen.start_burst = j.value("start_burst", gen.start_burst);
    gen.burst_growth_every = j.value("burst_growth_every", gen.burst_growth_every);
    gen.max_burst = j.value("max_burst", gen.max_burst);
    gen.safe_room_every = j.value("safe_room_every", gen.safe_room_every);
    gen.max_waves = j.value("max_waves", gen.max_waves);
    gen.level_growth = j.value("level_growth", gen.level_growth);
    gen.stat_per_level = j.value("stat_per_level", gen.stat_per_level);

    if (j.contains("overrides") && j["overrides"].is_object())
    {
        for (const auto& [key, val] : j["overrides"].items())
        {
            int waveNum = std::stoi(key);
            WaveOverride ov;
            ov.spawn_interval = val.value("spawn_interval", 0.5f);
            ov.burst_size = val.value("burst_size", 1);
            ov.safe_room_after = val.value("safe_room_after", false);
            if (val.contains("enemies") && val["enemies"].is_array())
            {
                for (const auto& gj : val["enemies"])
                {
                    WaveOverride::Group g;
                    g.config_path = gj.value("config", std::string{});
                    g.count = gj.value("count", 1);
                    ov.enemies.push_back(std::move(g));
                }
            }
            gen.overrides[waveNum] = std::move(ov);
        }
    }

    wc.loaded = true;
    std::cout << "[ConfigLoader] Loaded wave rules from " << filePath << " (" << gen.enemies.size()
              << " enemy types)\n";
    return true;
}

// ---------------------------------------------------------------------------
// Item definition loading
// ---------------------------------------------------------------------------

static ItemCategory parseCategory(const std::string& s)
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
    return ItemCategory::Material;
}

static Rarity parseRarity(const std::string& s)
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

static ArmorSlot parseArmorSlot(const std::string& s)
{
    if (s == "head")
        return ArmorSlot::Head;
    if (s == "legs")
        return ArmorSlot::Legs;
    if (s == "feet")
        return ArmorSlot::Feet;
    return ArmorSlot::Chest;
}

bool ConfigLoader::loadItemDefs(EntityManager& em, const std::string& dirPath)
{
    namespace fs = std::filesystem;

    auto& registry = em.registry().ctx().get<ItemRegistry>();
    int count = 0;

    std::error_code ec;
    for (const auto& entry : fs::recursive_directory_iterator(dirPath, ec))
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
            std::cerr << "[ConfigLoader] Error parsing item " << entry.path() << ": " << e.what()
                      << "\n";
            continue;
        }

        // Normalize path to use forward slashes and be relative to the exe.
        std::string configPath = entry.path().generic_string();

        ItemDef def;
        def.config_path = configPath;
        def.name = j.value("name", std::string{});
        def.description = j.value("description", std::string{});
        def.category = parseCategory(j.value("category", std::string{"material"}));
        def.rarity = parseRarity(j.value("rarity", std::string{"common"}));

        def.base_damage = j.value("base_damage", 0.0f);
        def.weight = j.value("weight", 0.5f);
        def.str_scaling = j.value("str_scaling", 0.0f);
        def.dex_scaling = j.value("dex_scaling", 0.0f);
        def.str_requirement = j.value("str_requirement", 0);
        def.dex_requirement = j.value("dex_requirement", 0);
        def.two_handed = j.value("two_handed", false);

        def.armor_slot = parseArmorSlot(j.value("armor_slot", std::string{"chest"}));
        def.defense_bonus = j.value("defense_bonus", 0.0f);
        def.poise_bonus = j.value("poise_bonus", 0.0f);
        def.max_guard = j.value("max_guard", 0.0f);

        def.max_durability = j.value("max_durability", 100.0f);
        def.stackable = j.value("stackable", false);
        def.max_stack = j.value("max_stack", 1);
        def.value = j.value("value", 0);

        registry.defs[configPath] = std::move(def);
        ++count;
    }

    if (ec)
    {
        std::cerr << "[ConfigLoader] Error scanning item dir " << dirPath << ": " << ec.message()
                  << "\n";
    }

    registry.loaded = (count > 0);
    std::cout << "[ConfigLoader] Loaded " << count << " item definitions from " << dirPath << "\n";
    return count > 0;
}

bool ConfigLoader::loadRecipes(EntityManager& em, const std::string& dirPath)
{
    namespace fs = std::filesystem;

    auto& registry = em.registry().ctx().get<RecipeRegistry>();
    int count = 0;

    std::error_code ec;
    for (const auto& entry : fs::recursive_directory_iterator(dirPath, ec))
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
            std::cerr << "[ConfigLoader] Error parsing recipe " << entry.path() << ": " << e.what()
                      << "\n";
            continue;
        }

        RecipeDef recipe;
        recipe.config_path = entry.path().generic_string();
        recipe.name = j.value("name", std::string{});
        recipe.output_item = j.value("output", std::string{});
        recipe.output_quantity = j.value("output_quantity", 1);

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
    {
        std::cerr << "[ConfigLoader] Error scanning recipe dir " << dirPath << ": " << ec.message()
                  << "\n";
    }

    registry.loaded = (count > 0);
    std::cout << "[ConfigLoader] Loaded " << count << " recipes from " << dirPath << "\n";
    return count > 0;
}
