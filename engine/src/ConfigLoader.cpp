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
    Sprite s;
    s.texture_path = j.value("texture_path", std::string{});
    s.src_x = j.value("src_x", 0);
    s.src_y = j.value("src_y", 0);
    s.src_w = j.value("src_w", 0);
    s.src_h = j.value("src_h", 0);
    s.layer = j.value("layer", 0);
    em.registry().emplace<Sprite>(entity, s);
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

// Parse a grade string ("S"–"E") to ScalingGrade enum.
static ScalingGrade parseGrade(const std::string& s)
{
    if (s == "S")
        return ScalingGrade::S;
    if (s == "A")
        return ScalingGrade::A;
    if (s == "B")
        return ScalingGrade::B;
    if (s == "C")
        return ScalingGrade::C;
    if (s == "D")
        return ScalingGrade::D;
    return ScalingGrade::E;
}

static void loadWeapon(EntityManager& em, entt::entity entity, const json& j)
{
    Weapon w;
    w.weight = j.value("weight", 0.5f);
    w.str_scaling = parseGrade(j.value("str_scaling", std::string{"E"}));
    w.dex_scaling = parseGrade(j.value("dex_scaling", std::string{"E"}));
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

static void loadAIController(EntityManager& em, entt::entity entity, const json& j)
{
    AIController ai;
    ai.aggro_radius = j.value("aggro_radius", 0.0f);
    ai.turn_speed = j.value("turn_speed", 8.0f);
    ai.separation_strength = j.value("separation_strength", 1.0f);
    ai.arrival_radius = j.value("arrival_radius", 0.0f);
    ai.attack_radius = j.value("attack_radius", 0.0f);
    ai.tier = j.value("tier", 1);

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
    {"ai_controller",    loadAIController},
    {"rest_spot",        loadRestSpot},
    {"solid_color",      loadSolidColor},
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
    if (j.contains("damage") && j["damage"].contains("grade_multipliers"))
    {
        const auto& gm = j["damage"]["grade_multipliers"];
        f.grade_multipliers.s = gm.value("S", f.grade_multipliers.s);
        f.grade_multipliers.a = gm.value("A", f.grade_multipliers.a);
        f.grade_multipliers.b = gm.value("B", f.grade_multipliers.b);
        f.grade_multipliers.c = gm.value("C", f.grade_multipliers.c);
        f.grade_multipliers.d = gm.value("D", f.grade_multipliers.d);
        f.grade_multipliers.e = gm.value("E", f.grade_multipliers.e);
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
        f.poise.weight_scale = j["poise"].value("weight_scale", f.poise.weight_scale);
        f.poise.stagger_duration = j["poise"].value("stagger_duration", f.poise.stagger_duration);
        f.poise.decay_window = j["poise"].value("decay_window", f.poise.decay_window);
    }

    f.loaded = true;
    std::cout << "[ConfigLoader] Loaded formulas from " << filePath << "\n";
    return true;
}
