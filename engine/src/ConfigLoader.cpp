#include "ConfigLoader.h"

#include "ecs/Components.h"

#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

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

    const auto& comps = data["components"];

    if (comps.contains("transform"))
    {
        const auto& j = comps["transform"];
        Transform t;
        t.x = j.value("x", 0.0f);
        t.y = j.value("y", 0.0f);
        t.rotation = j.value("rotation", 0.0f);
        t.scale = j.value("scale", 1.0f);
        em.registry().emplace<Transform>(entity, t);
    }

    if (comps.contains("velocity"))
    {
        const auto& j = comps["velocity"];
        Velocity v;
        v.dx = j.value("dx", 0.0f);
        v.dy = j.value("dy", 0.0f);
        em.registry().emplace<Velocity>(entity, v);
    }

    if (comps.contains("health"))
    {
        const auto& j = comps["health"];
        Health h;
        h.current = j.value("current", 0);
        h.max = j.value("max", 0);
        em.registry().emplace<Health>(entity, h);
    }

    if (comps.contains("sprite"))
    {
        const auto& j = comps["sprite"];
        Sprite s;
        s.textureId = j.value("texture_id", uint32_t{0});
        s.srcX = j.value("src_x", 0);
        s.srcY = j.value("src_y", 0);
        s.srcW = j.value("src_w", 0);
        s.srcH = j.value("src_h", 0);
        s.layer = j.value("layer", 0);
        em.registry().emplace<Sprite>(entity, s);
    }

    if (comps.contains("collider"))
    {
        const auto& j = comps["collider"];
        Collider c;
        c.width = j.value("width", 0.0f);
        c.height = j.value("height", 0.0f);
        c.isSolid = j.value("is_solid", true);
        em.registry().emplace<Collider>(entity, c);
    }

    return entity;
}
