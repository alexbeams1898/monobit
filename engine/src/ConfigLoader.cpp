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
    s.texturePath = j.value("texture_path", std::string{});
    s.srcX = j.value("src_x", 0);
    s.srcY = j.value("src_y", 0);
    s.srcW = j.value("src_w", 0);
    s.srcH = j.value("src_h", 0);
    s.layer = j.value("layer", 0);
    em.registry().emplace<Sprite>(entity, s);
}

static void loadCollider(EntityManager& em, entt::entity entity, const json& j)
{
    Collider c;
    c.width = j.value("width", 0.0f);
    c.height = j.value("height", 0.0f);
    c.isSolid = j.value("is_solid", true);
    em.registry().emplace<Collider>(entity, c);
}

// clang-format off
static const std::unordered_map<std::string, LoaderFn> kComponentLoaders = {
    {"transform", loadTransform},
    {"velocity",  loadVelocity},
    {"health",    loadHealth},
    {"sprite",    loadSprite},
    {"collider",  loadCollider},
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
