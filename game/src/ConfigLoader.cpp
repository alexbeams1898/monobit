#include "ConfigLoader.h"

#include "ecs/Components.h"
#include "ecs/GameComponents.h"
#include "ecs/GameConfig.h"

#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <nlohmann/json.hpp>
#include <random>
#include <unordered_map>

using json = nlohmann::json;

// Cache parsed JSON files in memory. First read hits disk; subsequent reads
// serve from cache. Eliminates per-spawn disk I/O (skeleton.json + animation
// sheet = 2 reads per enemy spawn without cache).
static std::unordered_map<std::string, json> sJsonCache;

static const json* cachedReadJson(const std::string& path)
{
    auto it = sJsonCache.find(path);
    if (it != sJsonCache.end())
        return &it->second;

    std::ifstream file(path);
    if (!file.is_open())
    {
        std::cerr << "[ConfigLoader] Cannot open: " << path << "\n";
        return nullptr;
    }

    try
    {
        auto [inserted, _] = sJsonCache.emplace(path, json::parse(file));
        return &inserted->second;
    }
    catch (const std::exception& e)
    {
        std::cerr << "[ConfigLoader] Error parsing " << path << ": " << e.what() << "\n";
        return nullptr;
    }
}

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

// Parse a single shape from JSON. Shape kind is determined by the "shape" field
// ("aabb", "circle", "capsule"). Missing shape field defaults to aabb.
static CollisionShape parseCollisionShape(const json& j)
{
    CollisionShape s;
    const std::string kind = j.value("shape", std::string{"aabb"});
    if (kind == "circle")
        s.kind = ShapeKind::Circle;
    else if (kind == "capsule")
        s.kind = ShapeKind::Capsule;
    else
        s.kind = ShapeKind::AABB;

    s.x = j.value("x", 0.0f);
    s.y = j.value("y", 0.0f);
    s.w = j.value("w", 0.0f);
    s.h = j.value("h", 0.0f);
    s.r = j.value("r", 0.0f);
    s.x2 = j.value("x2", 0.0f);
    s.y2 = j.value("y2", 0.0f);
    return s;
}

// Hurtbox config is a JSON array of shapes. Each shape may carry an optional
// "label" and "dmg_mult" (default 1.0). If no "hurtboxes" field exists on an
// entity, the post-load hook auto-generates a single AABB matching the Collider.
static void loadHurtbox(EntityManager& em, entt::entity entity, const json& j)
{
    Hurtbox hb;
    if (j.is_array())
    {
        for (const auto& sj : j)
        {
            HurtShape hs;
            hs.shape = parseCollisionShape(sj);
            hs.label = sj.value("label", std::string{});
            hs.dmg_mult = sj.value("dmg_mult", 1.0f);
            hb.shapes.push_back(hs);
        }
    }
    em.registry().emplace<Hurtbox>(entity, std::move(hb));
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

static void loadLadder(EntityManager& em, entt::entity entity, const json& j)
{
    Ladder l;
    l.radius = j.value("radius", 48.0f);
    l.spawn_duration = j.value("spawn_duration", 0.5f);
    em.registry().emplace<Ladder>(entity, l);
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

static void populateAnimRowIndex(EntityManager& em, const json& sheetData)
{
    if (!sheetData.contains("states"))
        return;
    auto* existing = em.registry().ctx().find<AnimRowIndex>();
    if (existing == nullptr)
        existing = &em.registry().ctx().emplace<AnimRowIndex>();
    auto& rowIndex = *existing;
    for (const auto& [key, val] : sheetData["states"].items())
    {
        if (rowIndex.rows.count(key) > 0)
            continue;
        AnimRowEntry entry;
        entry.row = val.value("row", 0);
        entry.frames = val.value("frames", 1);
        entry.duration = val.value("duration", 0.0f);
        rowIndex.rows[key] = entry;
    }
}

static void parseAnchorFrames(const json& arr, std::vector<HandAnchor>& out)
{
    static constexpr float DEG2RAD = 3.14159265f / 180.0f;
    out.clear();
    for (const auto& fr : arr)
    {
        HandAnchor a;
        a.x = fr[0].get<float>();
        a.y = fr[1].get<float>();
        if (fr.size() > 2)
            a.rotation = fr[2].get<float>() * DEG2RAD;
        if (fr.size() > 3)
            a.flip = fr[3].get<int>();
        if (fr.size() > 4)
            a.depth = fr[4].get<int>();
        out.push_back(a);
    }
}

static void parseAnchorCell(const json& cell, std::vector<HandAnchor>& left,
                            std::vector<HandAnchor>& right)
{
    if (cell.is_array())
    {
        parseAnchorFrames(cell, left);
        right.resize(left.size());
        for (size_t i = 0; i < left.size(); ++i)
        {
            right[i].x = -left[i].x;
            right[i].y = left[i].y;
        }
        return;
    }
    if (cell.is_object())
    {
        if (cell.contains("left"))
            parseAnchorFrames(cell["left"], left);
        if (cell.contains("right"))
            parseAnchorFrames(cell["right"], right);
    }
}

static void populateHandAnchors(EntityManager& em, const json& sheetData)
{
    if (!sheetData.contains("hand_anchors"))
        return;
    auto* existing = em.registry().ctx().find<HandAnchorData>();
    if (existing == nullptr)
        existing = &em.registry().ctx().emplace<HandAnchorData>();
    auto& anchors = *existing;

    const auto& ha = sheetData["hand_anchors"];
    if (ha.contains("depth_per_dir"))
    {
        anchors.depth_per_dir.clear();
        for (const auto& d : ha["depth_per_dir"])
            anchors.depth_per_dir.push_back(d.get<int>());
    }

    if (!ha.contains("rows"))
        return;
    const char* dirKeys[] = {"S", "W", "E", "N"};
    for (const auto& [rowKey, rowVal] : ha["rows"].items())
    {
        if (rowKey[0] == '_')
            continue;
        const int rowIdx = std::stoi(rowKey);
        auto& row = anchors.rows[rowIdx];
        row.left.resize(4);
        row.right.resize(4);
        for (int d = 0; d < 4; ++d)
        {
            if (!rowVal.contains(dirKeys[d]))
                continue;
            parseAnchorCell(rowVal[dirKeys[d]], row.left[d], row.right[d]);
        }
    }
}

static bool emplaceAnimationFromSheet(EntityManager& em, entt::entity entity,
                                      const std::string& sheetPath)
{
    if (sheetPath.empty())
        return false;

    const json* cached = cachedReadJson(sheetPath);
    if (cached == nullptr)
        return false;

    const json& sheetData = *cached;

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

    // Game-side row lookup: maps AnimState -> spritesheet row/frames/duration.
    AnimRowConfig rowCfg;

    auto loadState = [&](const char* name, AnimState state, bool freeze)
    {
        if (sheetData.contains("states") && sheetData["states"].contains(name))
        {
            const auto& s = sheetData["states"][name];
            auto& rd = rowCfg.rows[static_cast<int>(state)];
            rd.row = s.value("row", 0);
            rd.frames = s.value("frames", 1);
            rd.duration = s.value("duration", 0.0f);
            rd.freeze_on_last = freeze;
        }
    };

    loadState("idle", AnimState::Idle, false);
    loadState("walk", AnimState::Walk, false);
    loadState("attack", AnimState::Attack, true);
    loadState("hit", AnimState::Hit, true);
    loadState("death", AnimState::Death, true);
    loadState("run", AnimState::Run, false);

    // Compute layout from ALL states in the JSON (not just the 6 loaded into
    // AnimRowConfig) so the column stride matches the actual sheet layout.
    int maxF = 1;
    int maxRow = 0;
    if (sheetData.contains("states"))
    {
        for (const auto& [key, val] : sheetData["states"].items())
        {
            const int f = val.value("frames", 1);
            const int r = val.value("row", 0);
            maxF = std::max(maxF, f);
            if (f > 0)
                maxRow = std::max(maxRow, r);
        }
    }
    anim.max_frames_per_state = maxF;
    anim.row_count = maxRow + 1;

    anim.direction_count = sheetData.value("direction_count", 4);

    populateAnimRowIndex(em, sheetData);
    populateHandAnchors(em, sheetData);

    // Set initial playback from Idle row so the first frame renders correctly
    // even before AnimStateSystem runs.
    const auto& idle = rowCfg.rows[static_cast<int>(AnimState::Idle)];
    anim.current_row = idle.row;
    anim.current_frames = idle.frames;
    anim.current_duration = idle.duration;

    em.registry().emplace<Animation>(entity, anim);
    em.registry().emplace<AnimRowConfig>(entity, rowCfg);
    return true;
}

static void loadAnimation(EntityManager& em, entt::entity entity, const json& j)
{
    const std::string sheetPath = j.value("sheet", std::string{});
    if (!emplaceAnimationFromSheet(em, entity, sheetPath))
        em.registry().emplace<Animation>(entity);
}

static void loadAppearance(EntityManager& em, entt::entity entity, const json& j)
{
    const std::string sheetPath = j.value("sheet", std::string{});
    if (!emplaceAnimationFromSheet(em, entity, sheetPath))
        em.registry().emplace<Animation>(entity);

    AppearanceDef def;
    def.sheet_path = sheetPath;
    def.sprite_layer = j.value("layer", 2);

    if (j.contains("layers") && j["layers"].is_array())
    {
        for (const auto& l : j["layers"])
            def.layers.push_back(l.get<std::string>());
    }

    if (j.contains("layer_manifest"))
        def.layer_manifest = j.value("layer_manifest", std::string{});

    if (j.contains("default_layers") && j["default_layers"].is_object())
    {
        for (auto& [key, val] : j["default_layers"].items())
            def.default_layers[key] = val.get<std::string>();
    }

    auto& spr = em.registry().get_or_emplace<Sprite>(entity);
    spr.layer = def.sprite_layer;

    em.registry().emplace<AppearanceDef>(entity, std::move(def));
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
    ai.deaggro_radius = j.value("deaggro_radius", 0.0f);
    ai.turn_speed = j.value("turn_speed", 8.0f);
    ai.separation_strength = j.value("separation_strength", 1.0f);
    ai.arrival_radius = j.value("arrival_radius", 0.0f);
    ai.attack_radius = j.value("attack_radius", 0.0f);
    ai.speed_multiplier = j.value("speed_multiplier", 1.0f);
    ai.tier = j.value("tier", 1);
    ai.sprint_multiplier = j.value("sprint_multiplier", 0.0f);
    ai.sprint_threshold = j.value("sprint_threshold", 0.0f);
    ai.orbit_speed = j.value("orbit_speed", 0.5f);
    ai.attack_cooldown = j.value("attack_cooldown", 0.0f);

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

static void loadHitSound(EntityManager& em, entt::entity entity, const json& j)
{
    HitSound hs;
    hs.path = j.value("path", std::string{});
    hs.volume = j.value("volume", 0.5f);
    hs.min_pitch = j.value("min_pitch", 0.9f);
    hs.max_pitch = j.value("max_pitch", 1.1f);
    em.registry().emplace<HitSound>(entity, std::move(hs));
}

static void loadDeathSound(EntityManager& em, entt::entity entity, const json& j)
{
    DeathSound ds;
    ds.path = j.value("path", std::string{});
    ds.volume = j.value("volume", 0.5f);
    ds.min_pitch = j.value("min_pitch", 0.9f);
    ds.max_pitch = j.value("max_pitch", 1.1f);
    em.registry().emplace<DeathSound>(entity, std::move(ds));
}

static void loadAmbientSound(EntityManager& em, entt::entity entity, const json& j)
{
    AmbientSound amb;
    if (j.contains("paths") && j["paths"].is_array())
    {
        for (const auto& p : j["paths"])
            amb.paths.push_back(p.get<std::string>());
    }
    amb.volume = j.value("volume", 0.3f);
    amb.min_interval = j.value("min_interval", 3.0f);
    amb.max_interval = j.value("max_interval", 8.0f);
    amb.max_distance = j.value("max_distance", 400.0f);
    amb.min_pitch = j.value("min_pitch", 0.7f);
    amb.max_pitch = j.value("max_pitch", 0.9f);

    // Build initial shuffle order.
    static std::mt19937 sRng{std::random_device{}()};
    amb.shuffle_order.resize(amb.paths.size());
    for (int i = 0; i < static_cast<int>(amb.paths.size()); ++i)
        amb.shuffle_order[static_cast<size_t>(i)] = i;
    std::shuffle(amb.shuffle_order.begin(), amb.shuffle_order.end(), sRng);

    // Short initial timer so the first sound comes quickly after spawn.
    // Range [0, min_interval] still staggers multiple spawns.
    std::uniform_real_distribution<float> dist(0.0f, amb.min_interval);
    amb.timer = dist(sRng);

    em.registry().emplace<AmbientSound>(entity, std::move(amb));
}

static void loadAggroSound(EntityManager& em, entt::entity entity, const json& j)
{
    AggroSound aggro;
    if (j.contains("paths") && j["paths"].is_array())
    {
        for (const auto& p : j["paths"])
            aggro.paths.push_back(p.get<std::string>());
    }
    aggro.volume = j.value("volume", 0.4f);
    aggro.min_interval = j.value("min_interval", 3.0f);
    aggro.max_interval = j.value("max_interval", 8.0f);
    aggro.max_distance = j.value("max_distance", 400.0f);
    aggro.min_pitch = j.value("min_pitch", 0.85f);
    aggro.max_pitch = j.value("max_pitch", 1.15f);

    // Build initial shuffle order.
    static std::mt19937 sRng{std::random_device{}()};
    aggro.shuffle_order.resize(aggro.paths.size());
    for (int i = 0; i < static_cast<int>(aggro.paths.size()); ++i)
        aggro.shuffle_order[static_cast<size_t>(i)] = i;
    std::shuffle(aggro.shuffle_order.begin(), aggro.shuffle_order.end(), sRng);

    em.registry().emplace<AggroSound>(entity, std::move(aggro));
}

// clang-format off
static const std::unordered_map<std::string, LoaderFn> kComponentLoaders = {
    {"transform",        loadTransform},
    {"velocity",         loadVelocity},
    {"health",           loadHealth},
    {"sprite",           loadSprite},
    {"collider",         loadCollider},
    {"hurtbox",          loadHurtbox},
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
    {"ladder",           loadLadder},
    {"solid_color",      loadSolidColor},
    {"inventory",        loadInventory},
    {"equipment",        loadEquipment},
    {"body",             loadBody},
    {"hit_sound",        loadHitSound},
    {"death_sound",      loadDeathSound},
    {"ambient_sound",    loadAmbientSound},
    {"aggro_sound",      loadAggroSound},
    {"appearance",       loadAppearance},
};
// clang-format on

entt::entity ConfigLoader::loadEntity(EntityManager& em, const std::string& filePath)
{
    const json* cached = cachedReadJson(filePath);
    if (cached == nullptr)
        return entt::null;

    const json& data = *cached;

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

    // Auto-generate a default Hurtbox from the entity's Collider if one wasn't
    // explicitly configured. This keeps backwards compatibility: entities with
    // no "hurtbox" in config behave identically to pre-refactor code that used
    // Collider geometry for damage.
    auto& reg = em.registry();
    if (reg.all_of<Collider>(entity) && !reg.all_of<Hurtbox>(entity))
    {
        const auto& col = reg.get<Collider>(entity);
        Hurtbox hb;
        HurtShape hs;
        hs.shape.kind = ShapeKind::AABB;
        hs.shape.w = col.width;
        hs.shape.h = col.height;
        hs.label = "default";
        hs.dmg_mult = 1.0f;
        hb.shapes.push_back(hs);
        reg.emplace<Hurtbox>(entity, std::move(hb));
    }

    return entity;
}

static void loadCombatAIConfig(const json& j, FormulaConfig& f)
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

static void loadCombatConfig(const json& j, FormulaConfig& f)
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
        f.movement.backpedal_multiplier =
            j["movement"].value("backpedal_multiplier", f.movement.backpedal_multiplier);
        f.movement.sprint_anim_speed =
            j["movement"].value("sprint_anim_speed", f.movement.sprint_anim_speed);
        f.movement.backpedal_anim_speed =
            j["movement"].value("backpedal_anim_speed", f.movement.backpedal_anim_speed);
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

    if (j.contains("weapon_xp"))
    {
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
        f.weapon_xp.power_level_weight =
            wx.value("power_level_weight", f.weapon_xp.power_level_weight);
        f.weapon_xp.power_hp_weight = wx.value("power_hp_weight", f.weapon_xp.power_hp_weight);
        f.weapon_xp.power_dmg_weight = wx.value("power_dmg_weight", f.weapon_xp.power_dmg_weight);
        f.weapon_xp.power_stat_weight =
            wx.value("power_stat_weight", f.weapon_xp.power_stat_weight);
        f.weapon_xp.power_base = wx.value("power_base", f.weapon_xp.power_base);
        f.weapon_xp.power_dmg_factor = wx.value("power_dmg_factor", f.weapon_xp.power_dmg_factor);
        f.weapon_xp.power_rarity_factor =
            wx.value("power_rarity_factor", f.weapon_xp.power_rarity_factor);
    }

    if (j.contains("equip_load"))
    {
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

    loadCombatAIConfig(j, f);
    loadCombatConfig(j, f);

    if (j.contains("steering"))
    {
        const auto& st = j["steering"];
        em.steering_config.repulsion_radius =
            st.value("repulsion_radius", em.steering_config.repulsion_radius);
        em.steering_config.repulsion_strength =
            st.value("repulsion_strength", em.steering_config.repulsion_strength);
        em.steering_config.blend_rate = st.value("blend_rate", em.steering_config.blend_rate);
        em.steering_config.skip_dot_threshold =
            st.value("skip_dot_threshold", em.steering_config.skip_dot_threshold);
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

    // Iterate every key in the JSON object — fully data-driven.
    for (auto& [key, val] : j.items())
    {
        SoundEntry entry;
        entry.path = val.value("path", std::string{});
        entry.volume = val.value("volume", 0.5f);
        if (val.contains("variations"))
        {
            for (const auto& v : val["variations"])
                entry.variations.push_back(v.get<std::string>());
        }
        s.entries[key] = std::move(entry);
    }

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

    mc.main_menu_rare_chance = j.value("main_menu_rare_chance", 0.0f);

    // Auto-parse named tracks: any top-level key that is an object with a
    // "path" field (skip "tracks" array and scalar values).
    for (auto& [key, val] : j.items())
    {
        if (key == "tracks" || !val.is_object())
            continue;
        MusicConfig::Track track;
        track.path = val.value("path", "");
        track.volume = val.value("volume", mc.default_volume);
        track.fade_in_ms = val.value("fade_in_ms", 0);
        if (!track.path.empty())
            mc.named[key] = std::move(track);
    }

    std::cout << "[ConfigLoader] Loaded " << mc.tracks.size() << " music tracks, "
              << mc.named.size() << " named tracks from " << filePath << "\n";
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
            const int waveNum = std::stoi(key);
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
    if (s == "accessory")
        return ItemCategory::Accessory;
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
        const std::string configPath = entry.path().generic_string();

        ItemDef def;
        def.config_path = configPath;
        def.name = j.value("name", std::string{});
        def.description = j.value("description", std::string{});
        def.icon_path = j.value("icon", std::string{});
        def.category = parseCategory(j.value("category", std::string{"material"}));
        def.rarity = parseRarity(j.value("rarity", std::string{"common"}));

        def.base_damage = j.value("base_damage", 0.0f);
        def.weight = j.value("weight", 0.5f);
        def.str_scaling = j.value("str_scaling", 0.0f);
        def.dex_scaling = j.value("dex_scaling", 0.0f);
        def.str_requirement = j.value("str_requirement", 0);
        def.dex_requirement = j.value("dex_requirement", 0);
        def.two_handed = j.value("two_handed", false);
        def.weapon_tier = j.value("weapon_tier", std::string{});
        def.damage_per_level = j.value("damage_per_level", -1.0f);
        def.scaling_per_level = j.value("scaling_per_level", -1.0f);

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
        def.weapon_icon = j.value("weapon_icon", std::string{});
        def.grip_x = j.value("grip_x", 0.0f);
        def.grip_y = j.value("grip_y", 0.0f);
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

        // Parse weapon hitbox shapes. Each entry is a shape object plus
        // optional label / dmg_mult / priority fields.
        if (j.contains("hitboxes") && j["hitboxes"].is_array())
        {
            for (const auto& hbj : j["hitboxes"])
            {
                WeaponHitboxShape hs;
                hs.shape = parseCollisionShape(hbj);
                hs.label = hbj.value("label", std::string{});
                hs.dmg_mult = hbj.value("dmg_mult", 1.0f);
                hs.priority = hbj.value("priority", 0);
                def.hitboxes.push_back(hs);
            }
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

bool ConfigLoader::loadScoring(EntityManager& em, const std::string& filePath)
{
    std::ifstream file(filePath);
    if (!file.is_open())
    {
        std::cerr << "[ConfigLoader] Cannot open scoring: " << filePath << " -- using defaults\n";
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
                  << " -- using defaults\n";
        return false;
    }

    ScoringConfig& sc = em.registry().ctx().get<ScoringConfig>();
    sc.kill_weight = j.value("kill_weight", sc.kill_weight);
    sc.wave_weight = j.value("wave_weight", sc.wave_weight);
    sc.time_penalty_weight = j.value("time_penalty_weight", sc.time_penalty_weight);
    sc.xp_weight = j.value("xp_weight", sc.xp_weight);
    sc.money_weight = j.value("money_weight", sc.money_weight);
    sc.escape_multiplier = j.value("escape_multiplier", sc.escape_multiplier);
    sc.loaded = true;

    std::cout << "[ConfigLoader] Loaded scoring config from " << filePath << "\n";
    return true;
}

bool ConfigLoader::loadWeaponTiers(EntityManager& em, const std::string& filePath)
{
    std::ifstream file(filePath);
    if (!file.is_open())
    {
        std::cout << "[ConfigLoader] No weapon tiers file: " << filePath << " -- using defaults\n";
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

    auto& registry = em.registry().ctx().get<WeaponTierRegistry>();
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
    std::cout << "[ConfigLoader] Loaded " << count << " weapon tiers from " << filePath << "\n";
    return count > 0;
}

static EvolutionNode parseEvolutionNode(const json& nodeJson)
{
    EvolutionNode node;
    node.weapon_config_path = nodeJson.value("weapon", std::string{});

    if (nodeJson.contains("evolutions") && nodeJson["evolutions"].is_array())
    {
        for (const auto& ej : nodeJson["evolutions"])
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

bool ConfigLoader::loadEvolutionTrees(EntityManager& em, const std::string& dirPath)
{
    namespace fs = std::filesystem;

    auto& registry = em.registry().ctx().get<EvolutionRegistry>();
    int count = 0;

    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dirPath, ec))
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
            std::cerr << "[ConfigLoader] Error parsing evolution tree " << entry.path() << ": "
                      << e.what() << "\n";
            continue;
        }

        EvolutionFamily family;
        family.name = j.value("family", std::string{});

        if (j.contains("nodes") && j["nodes"].is_object())
        {
            for (auto it = j["nodes"].begin(); it != j["nodes"].end(); ++it)
                family.nodes[it.key()] = parseEvolutionNode(it.value());
        }

        const int familyIdx = static_cast<int>(registry.families.size());
        for (const auto& [nodeId, node] : family.nodes)
        {
            if (!node.weapon_config_path.empty())
                registry.weapon_to_node[node.weapon_config_path] = {familyIdx, nodeId};
        }

        registry.families.push_back(std::move(family));
        ++count;
    }

    if (ec)
    {
        std::cout << "[ConfigLoader] Evolution dir not found: " << dirPath << " -- skipping\n";
    }

    registry.loaded = (count > 0);
    if (count > 0)
        std::cout << "[ConfigLoader] Loaded " << count << " evolution trees from " << dirPath
                  << "\n";
    return count > 0;
}

static AppearanceOption parseAppearanceOption(const json& opt)
{
    AppearanceOption o;
    o.id = opt.value("id", std::string{});
    o.label = opt.value("label", o.id);
    o.file = opt.value("file", std::string{});
    if (opt.contains("swatch"))
    {
        const std::string hex = opt.value("swatch", std::string{});
        if (hex.size() == 7 && hex[0] == '#')
        {
            const uint32_t rgb = std::stoul(hex.substr(1), nullptr, 16);
            o.swatch = (rgb << 8) | 0xFF;
        }
    }
    return o;
}

static AppearanceCategory parseAppearanceCategory(const json& cat)
{
    AppearanceCategory c;
    c.id = cat.value("id", std::string{});
    c.label = cat.value("label", c.id);
    c.required = cat.value("required", false);
    c.hidden = cat.value("hidden", false);
    c.path_prefix = cat.value("path_prefix", std::string{});
    c.linked_to = cat.value("linked_to", std::string{});
    c.combine_with = cat.value("combine_with", std::string{});
    c.palette_id = cat.value("palette_id", std::string{});
    c.base_color = cat.value("base_color", std::string{});
    c.master_file = cat.value("master_file", std::string{});
    c.palette_from_combine = cat.value("palette_from_combine", false);

    const std::string type_str = cat.value("type", std::string{"select"});
    if (type_str == "slider")
    {
        c.type = AppearanceCategoryType::Slider;
        c.min_value = cat.value("min", 0.0f);
        c.max_value = cat.value("max", 1.0f);
        c.step_value = cat.value("step", 0.01f);
        c.default_value = cat.value("default", c.min_value);
    }
    else if (cat.contains("options") && cat["options"].is_array())
    {
        for (const auto& opt : cat["options"])
            c.options.push_back(parseAppearanceOption(opt));
    }
    return c;
}

bool ConfigLoader::loadAppearanceConfig(EntityManager& em, const std::string& filePath)
{
    std::ifstream file(filePath);
    if (!file.is_open())
    {
        std::cout << "[ConfigLoader] No appearance config: " << filePath << " -- skipping\n";
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

    auto& cfg = em.registry().ctx().get<AppearanceConfig>();
    cfg.frame_size = j.value("frame_size", 64);

    if (j.contains("categories") && j["categories"].is_array())
    {
        for (const auto& cat : j["categories"])
            cfg.categories.push_back(parseAppearanceCategory(cat));
    }

    // Load palette swap files referenced by categories.
    auto& palReg = em.registry().ctx().emplace<PaletteRegistry>();
    if (j.contains("palettes") && j["palettes"].is_object())
    {
        for (const auto& [palId, palPath] : j["palettes"].items())
        {
            std::ifstream pf(palPath.get<std::string>());
            if (!pf.is_open())
                continue;
            json pj;
            try
            {
                pf >> pj;
            }
            catch (...)
            {
                continue;
            }
            auto& pal = palReg.palettes[palId];
            for (const auto& [colorName, hexArr] : pj.items())
            {
                auto& colors = pal[colorName];
                for (const auto& hex : hexArr)
                {
                    const std::string h = hex.get<std::string>();
                    if (h.size() < 7)
                        continue;
                    const auto r = static_cast<uint8_t>(std::stoi(h.substr(1, 2), nullptr, 16));
                    const auto g = static_cast<uint8_t>(std::stoi(h.substr(3, 2), nullptr, 16));
                    const auto b = static_cast<uint8_t>(std::stoi(h.substr(5, 2), nullptr, 16));
                    colors.push_back({r, g, b});
                }
            }
        }
    }

    cfg.loaded = true;
    std::cout << "[ConfigLoader] Loaded appearance config from " << filePath << " ("
              << cfg.categories.size() << " categories, " << palReg.palettes.size()
              << " palettes)\n";
    return true;
}
