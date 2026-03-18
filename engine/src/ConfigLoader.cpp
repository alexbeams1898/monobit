#include "ConfigLoader.h"

#include "ecs/Components.h"

#include <fstream>
#include <functional>
#include <iostream>
#include <nlohmann/json.hpp>
#include <unordered_map>

using json = nlohmann::json;

// ---------------------------------------------------------------------------
// Component loader table.
//
// Each entry maps a JSON key (e.g. "transform") to a function that reads
// that block and emplaces the corresponding component onto the entity.
//
// Adding a new component = add one static function below + one line in the
// table. The main loadEntity loop never needs to change.
//
// JS analogy: this is an object whose keys are component names and whose
// values are handler functions — exactly like a Redux action-type dispatch
// table.
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// Stat system loaders
// ---------------------------------------------------------------------------

static void loadStats(EntityManager& em, entt::entity entity, const json& j)
{
    Stats s;
    s.str = j.value("str", 1);
    s.dex = j.value("dex", 1);
    s.end = j.value("end", 1);
    s.lck = j.value("lck", 1);
    em.registry().emplace<Stats>(entity, s);
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
    // No JSON fields — attach with default (facing right).
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

// ---------------------------------------------------------------------------

static void loadLoot(EntityManager& em, entt::entity entity, const json& j)
{
    Loot l;
    l.xp_drop = j.value("xp_drop", 20);
    l.money_drop = j.value("money_drop", 0);
    em.registry().emplace<Loot>(entity, l);
}

// Shared helper: parse a sprite sheet sidecar JSON and emplace Animation + Sprite
// on the given entity. Used by both loadAnimation and loadBodyParts.
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

// Create child entities for split-body rendering. Each child gets its own
// Sprite + Animation with an independent direction source (velocity or aim).
// The parent keeps all gameplay components but has no Sprite/Animation.
static void loadBodyParts(EntityManager& em, entt::entity parent, const json& j)
{
    const Transform* parentTransform = em.registry().try_get<Transform>(parent);

    for (const auto& part : j)
    {
        auto child = em.create();

        BodyPart bp;
        bp.parent = parent;
        bp.faces_aim = part.value("faces_aim", false);
        em.registry().emplace<BodyPart>(child, bp);

        // Copy parent position so the child renders at the correct spot.
        Transform t;
        if (parentTransform)
        {
            t.x = parentTransform->x;
            t.y = parentTransform->y;
            t.scale = parentTransform->scale;
        }
        em.registry().emplace<Transform>(child, t);

        // Load animation sheet (also creates Sprite component on the child).
        const std::string sheetPath = part.value("sheet", std::string{});
        emplaceAnimationFromSheet(em, child, sheetPath);

        // Set the sprite layer from draw_order for z-ordering within the character.
        if (em.registry().all_of<Sprite>(child))
            em.registry().get<Sprite>(child).layer = part.value("draw_order", 0);

        em.registry().emplace<Tag>(child, Tag{"body_part"});
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
        // If an aggro radius is set, start Idle — AggroSystem transitions to
        // Chase when the player steps within range.  Without a radius, go
        // straight to Chase so existing configs that omit aggro_radius are
        // unaffected.
        ai.state =
            (ai.aggro_radius > 0.0f) ? AIController::State::Idle : AIController::State::Chase;
    }
    else if (behavior == "attack")
        ai.state = AIController::State::Attack;
    // else: default Idle

    em.registry().emplace<AIController>(entity, ai);
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
    {"shield",           loadShield},
    {"poise",            loadPoise},
    {"loot",             loadLoot},
    {"animation",        loadAnimation},
    {"ai_controller",    loadAIController},
    {"rest_spot",        loadRestSpot},
    {"solid_color",      loadSolidColor},
    {"body_parts",       loadBodyParts},
};
// clang-format on

// ---------------------------------------------------------------------------

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

    // Tag — always attached; name comes from the "tag" field or is left empty.
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
                  << " — using hardcoded defaults\n";
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
                  << " — using hardcoded defaults\n";
        return false;
    }

    FormulaConfig& f = em.formulas;

    if (j.contains("hp"))
    {
        f.hp.base = j["hp"].value("base", f.hp.base);
        f.hp.scale = j["hp"].value("scale", f.hp.scale);
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
                  << " — using hardcoded defaults\n";
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
                  << " — using hardcoded defaults\n";
        return false;
    }

    SoundConfig& s = em.sounds;

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

    s.loaded = true;
    std::cout << "[ConfigLoader] Loaded sounds from " << filePath << "\n";
    return true;
}
