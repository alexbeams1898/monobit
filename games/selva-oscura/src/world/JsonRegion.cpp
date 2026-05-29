#include "world/JsonRegion.h"

#include "Tunables.h"
#include "physics/PhysicsWorld.h"
#include "render/RegionShaders.h"
#include "world/Terrain.h"

#include <cmath>
#include <cstdio>

#include <glad/glad.h>

namespace selva::world
{

namespace
{

engine::physics::SurfaceTag parseSurfaceTag(const std::string& s)
{
    if (s == "Terrain")
        return engine::physics::SurfaceTag::Terrain;
    if (s == "Architecture")
        return engine::physics::SurfaceTag::Architecture;
    if (s == "Foliage")
        return engine::physics::SurfaceTag::Foliage;
    if (s == "Actor")
        return engine::physics::SurfaceTag::Actor;
    return engine::physics::SurfaceTag::Unknown;
}

engine::world::TransitionMode parseTransitionMode(const std::string& s)
{
    if (s == "Instant")
        return engine::world::TransitionMode::Instant;
    if (s == "Continuous")
        return engine::world::TransitionMode::Continuous;
    return engine::world::TransitionMode::Fade;
}

engine::world::TerrainModifier::Mode parseModifierMode(const std::string& s)
{
    using M = engine::world::TerrainModifier::Mode;
    if (s == "FlushSlope")
        return M::FlushSlope;
    if (s == "DepressTo")
        return M::DepressTo;
    if (s == "AddDelta")
        return M::AddDelta;
    return M::FlushAt;
}

engine::world::RegionKind parseRegionKind(const std::string& s)
{
    if (s == "Interior")
        return engine::world::RegionKind::Interior;
    return engine::world::RegionKind::Exterior;
}

glm::vec3 parseVec3(const nlohmann::json& a, glm::vec3 def = {0, 0, 0})
{
    if (!a.is_array() || a.size() < 3)
        return def;
    return glm::vec3(a[0].get<float>(), a[1].get<float>(), a[2].get<float>());
}

glm::vec2 parseVec2(const nlohmann::json& a, glm::vec2 def = {0, 0})
{
    if (!a.is_array() || a.size() < 2)
        return def;
    return glm::vec2(a[0].get<float>(), a[1].get<float>());
}

} // namespace

JsonRegion::JsonRegion(const nlohmann::json& json_doc, std::string folder)
    : engine::world::AsyncCapableRegion(
          json_doc.value("region_id", std::string{}), json_doc.value("debug_name", std::string{}),
          parseRegionKind(json_doc.value("region_kind", std::string{"Exterior"}))),
      region_json(json_doc), region_folder(std::move(folder))
{
}

void JsonRegion::preloadAssets()
{
    if (is_preloaded)
        return; // idempotent — lazy callers can call freely
    std::fprintf(stderr, "[json-region '%s'] preloadAssets START\n", regionId().c_str());
    // File I/O + GL upload + Jolt SHAPE construction happen HERE,
    // once at boot. Per-activation commit reuses preloaded shapes
    // to insert bodies in O(1) — no per-transition BVH rebuilds.
    // (Terrain alone is 73k tris and rebuilding its MeshShape on
    // every region-exit costs ~400ms in Debug. Shape preload keeps
    // transitions truly instant.)
    const auto& meshes_json = region_json.value("static_meshes", nlohmann::json::array());
    for (const auto& m : meshes_json)
    {
        auto lm = std::make_unique<LoadedMesh>();
        lm->path = m.value("path", std::string{});
        lm->world_origin = parseVec3(m.value("world_origin", nlohmann::json::array()));
        lm->tag = parseSurfaceTag(m.value("surface_tag", std::string{"Architecture"}));
        lm->debug_name = m.value("debug_name", std::string{});
        if (!loadStaticMesh(lm->path.c_str(), lm->world_origin, lm->mesh))
        {
            std::fprintf(stderr, "[json-region '%s'] failed to load mesh: %s\n", regionId().c_str(),
                         lm->path.c_str());
            continue;
        }
        lm->loaded = true;
        // Build Jolt shape per primitive so activation can skip the
        // BVH build (small primitives are cheap individually but
        // 444 of them per chapel adds up).
        lm->shape_handles.reserve(lm->mesh.primitives.size());
        for (const auto& prim : lm->mesh.primitives)
        {
            // Push a kInvalidShape slot for skipped/invalid prims so the
            // shape_handles index always parallels primitives index;
            // commitPrepared depends on that alignment.
            if (prim.cpu_positions.empty() || prim.cpu_indices.size() < 3 ||
                prim.usage == StaticMeshUsage::Visual)
            {
                lm->shape_handles.push_back(engine::physics::kInvalidShape);
                continue;
            }
            lm->shape_handles.push_back(
                engine::physics::createStaticTrimeshShape(prim.cpu_positions, prim.cpu_indices));
        }
        std::fprintf(stderr, "[json-region '%s'] preloaded mesh '%s' (%zu prims, %zu shapes)\n",
                     regionId().c_str(), lm->debug_name.c_str(), lm->mesh.primitives.size(),
                     lm->shape_handles.size());
        loaded_meshes.push_back(std::move(lm));
    }

    // Terrain shapes — built once at boot for whichever scenes
    // declare `terrain`. Even though terrain regions are global
    // today, preloading per region that uses them keeps the shape
    // tied to the region's lifetime conceptually.
    if (region_json.contains("terrain") && !region_json["terrain"].is_null())
    {
        for (int i = 0; i < selva::world::terrainRegionCount(); ++i)
        {
            const auto& r = selva::world::terrainRegion(i);
            if (r.cpu_positions.empty() || r.cpu_indices.size() < 3)
            {
                terrain_shapes.push_back(engine::physics::kInvalidShape);
                continue;
            }
            terrain_shapes.push_back(
                engine::physics::createStaticTrimeshShape(r.cpu_positions, r.cpu_indices));
            std::fprintf(stderr, "[json-region '%s'] preloaded terrain shape '%s'\n",
                         regionId().c_str(), r.name.c_str());
        }
    }

    std::fprintf(stderr, "[json-region '%s'] preloadAssets END (%zu meshes, %zu terrain shapes)\n",
                 regionId().c_str(), loaded_meshes.size(), terrain_shapes.size());
    is_preloaded = true;
}

void JsonRegion::commitPrepared(engine::world::RegionActivationContext& ctx)
{
    // Lazy preload: if a region wasn't preloaded at boot (e.g.
    // chapel_interior, which only loads when the player crosses
    // the door trigger), do it now. Idempotent — preloadAssets
    // short-circuits if already loaded.
    if (!is_preloaded)
        preloadAssets();
    std::fprintf(stderr, "[json-region '%s'] commitPrepared (activation) START\n",
                 regionId().c_str());
    // All Jolt shapes are preloaded in preloadAssets. This pass
    // only inserts bodies referencing those cached shapes — O(1)
    // per body, sub-ms total even for 444-primitive chapels +
    // 73k-tri terrain.

    // ---- Terrain bodies ----
    for (std::size_t i = 0; i < terrain_shapes.size(); ++i)
    {
        const auto shape = terrain_shapes[i];
        if (shape == engine::physics::kInvalidShape)
            continue;
        const auto& r = selva::world::terrainRegion(static_cast<int>(i));
        auto h = engine::physics::addStaticBodyFromShape(
            shape, engine::physics::SurfaceTag::Terrain, r.name.c_str());
        if (h != engine::physics::kInvalidBody)
            ctx.addBody(h);
    }

    // ---- Static mesh bodies ----
    for (const auto& lm : loaded_meshes)
    {
        if (!lm->loaded)
            continue;
        for (std::size_t i = 0; i < lm->mesh.primitives.size() && i < lm->shape_handles.size(); ++i)
        {
            const auto shape = lm->shape_handles[i];
            if (shape == engine::physics::kInvalidShape)
                continue;
            const auto& prim = lm->mesh.primitives[i];
            const std::string body_name = lm->debug_name + ":" + prim.source_node_name;
            auto h = engine::physics::addStaticBodyFromShape(shape, lm->tag, body_name.c_str());
            if (h != engine::physics::kInvalidBody)
                ctx.addBody(h);
        }
    }

    // ---- Terrain modifiers ----
    // NOTE: modifiers must be registered BEFORE the terrain region is
    // built, since the build queries the registry per vertex. For
    // scenes whose terrain is rebuilt at activation (TODO: per-region
    // terrain), this is automatic. For the current global terrain
    // (only SurfaceRegion uses it, baked once at boot), modifiers are
    // applied at boot and changing them after requires a rebuild.
    // Until per-region terrain ships, modifiers in region.json are
    // applied as the engine sees them.
    const auto& mods_json = region_json.value("terrain_modifiers", nlohmann::json::array());
    for (const auto& m : mods_json)
    {
        engine::world::TerrainModifier mod;
        mod.center_xz = parseVec2(m.value("center_xz", nlohmann::json::array()));
        mod.half_extents_xz = parseVec2(m.value("half_extents_xz", nlohmann::json::array()));
        mod.mode = parseModifierMode(m.value("mode", std::string{"FlushAt"}));
        mod.value = m.value("value", 0.0f);
        mod.value_far = m.value("value_far", 0.0f);
        const std::string ax = m.value("slope_axis", std::string{"Z"});
        mod.slope_axis = (ax == "X") ? 0 : 2;
        mod.blend_pad = m.value("blend_pad", 0.0f);
        // debug_name is a c-string in the engine struct; we can't
        // easily own a string here long enough. Skip for now (overlay
        // labels become "" for JSON-driven modifiers). Fix when the
        // overlay needs them.
        mod.debug_name = nullptr;
        engine::world::registerTerrainModifier(mod);
    }

    // ---- Triggers ----
    const auto& trigs_json = region_json.value("triggers", nlohmann::json::array());
    for (const auto& t : trigs_json)
    {
        engine::world::RegionTrigger trig;
        trig.id = t.value("id", std::string{});
        trig.center = parseVec3(t.value("center", nlohmann::json::array()));
        trig.half_extents = parseVec3(t.value("half_extents", nlohmann::json::array()));
        const std::string target_region_id = t.value("target_region", std::string{});
        trig.target = engine::world::findRegionId(target_region_id.c_str());
        trig.preserve_player_pos = t.value("preserve_player_pos", false);
        trig.target_spawn_pos = parseVec3(t.value("target_spawn_pos", nlohmann::json::array()));
        trig.override_yaw = t.value("override_yaw", false);
        trig.target_yaw = t.value("target_yaw", 0.0f);
        trig.mode = parseTransitionMode(t.value("transition_mode", std::string{"Fade"}));
        trig.fade_duration_seconds = t.value("fade_duration_seconds", 0.4f);
        trig.debug_name = t.value("debug_name", std::string{});
        ctx.addTrigger(trig);
        if (trig.target == engine::world::kInvalidRegion)
        {
            std::fprintf(stderr, "[json-region '%s'] trigger '%s' targets unknown region '%s'\n",
                         regionId().c_str(), trig.id.c_str(), target_region_id.c_str());
        }
    }
    std::fprintf(stderr,
                 "[json-region '%s'] commitPrepared END (%zu meshes, %zu mods, %zu triggers)\n",
                 regionId().c_str(), loaded_meshes.size(), mods_json.size(), trigs_json.size());
}

void JsonRegion::onDeactivate()
{
    // Resident-all-scenes: GL resources stay allocated across the
    // game session so re-activation is sub-ms. They're freed at
    // game shutdown via freeAssets(). Body cleanup is automatic
    // via the engine's owned-body tracking.
}

void JsonRegion::freeAssets()
{
    for (auto& lm : loaded_meshes)
    {
        if (lm->loaded)
            freeStaticMeshGLResources(lm->mesh);
    }
    loaded_meshes.clear();
}

void JsonRegion::renderMeshesDepth() const
{
    // Depth pass: shader + model matrix already bound by caller.
    // Just draw the primitives. No color/tint state (different
    // shader program).
    for (const auto& lm : loaded_meshes)
    {
        if (!lm->loaded)
            continue;
        for (const auto& p : lm->mesh.primitives)
        {
            if (p.vao == 0)
                continue;
            if (p.usage == StaticMeshUsage::Collision)
                continue; // physics-only proxy — not drawn
            glBindVertexArray(p.vao);
            glDrawElements(GL_TRIANGLES, p.index_count, GL_UNSIGNED_INT, nullptr);
        }
    }
}

void JsonRegion::renderMeshes() const
{
    // Draw every primitive of every loaded mesh at identity model
    // (positions are already world-space thanks to loadStaticMesh's
    // world_origin bake-in). Tint reset to 1.0 each draw so a
    // tinted skeletal pass that ran earlier this frame doesn't
    // bleed into our base-color path (the legacy chapel render had
    // the same guard).
    const bool debug_id = selva::tuning::current().debug_primitive_id_colors;
    static bool sLoggedIdMap = false;
    FILE* idmap_log = nullptr;
    if (debug_id && !sLoggedIdMap)
    {
        idmap_log = std::fopen("primitive-id-debug.log", "w");
        sLoggedIdMap = true;
    }
    selva::render::setSceneModel(glm::mat4(1.0f));
    int draw_idx = 0;
    for (const auto& lm : loaded_meshes)
    {
        if (!lm->loaded)
            continue;
        for (const auto& p : lm->mesh.primitives)
        {
            if (p.vao == 0)
                continue;
            if (p.usage == StaticMeshUsage::Collision)
                continue; // physics-only proxy — not drawn
            selva::render::setSceneTint(1.0f);
            if (debug_id)
            {
                // Deterministic per-primitive color: pack draw_idx
                // into 8-bit-per-channel RGB so consecutive primitives
                // get visibly distinct colors and the index can be
                // read back from a screenshot.
                const float r = static_cast<float>((draw_idx * 73) & 0xFF) / 255.0f;
                const float g = static_cast<float>((draw_idx * 151) & 0xFF) / 255.0f;
                const float b = static_cast<float>((draw_idx * 211) & 0xFF) / 255.0f;
                selva::render::setSceneBaseColor(glm::vec3(r, g, b));
                if (idmap_log != nullptr)
                {
                    std::fprintf(idmap_log, "%d  rgb=(%.3f,%.3f,%.3f)  name='%s:%s'\n", draw_idx, r,
                                 g, b, lm->debug_name.c_str(), p.source_node_name.c_str());
                }
            }
            else
            {
                selva::render::setSceneBaseColor(
                    glm::vec3(p.base_color[0], p.base_color[1], p.base_color[2]));
            }
            glBindVertexArray(p.vao);
            glDrawElements(GL_TRIANGLES, p.index_count, GL_UNSIGNED_INT, nullptr);
            ++draw_idx;
        }
    }
    if (idmap_log != nullptr)
        std::fclose(idmap_log);
}

} // namespace selva::world
