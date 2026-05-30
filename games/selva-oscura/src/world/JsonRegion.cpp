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
          // region_id is required; .at() throws on missing so the loader
          // refuses to construct a nameless region (which would never
          // resolve via findRegionId and would silently break the world).
          // debug_name + region_kind are optional with sensible defaults.
          json_doc.at("region_id").get<std::string>(),
          json_doc.value("debug_name", std::string{}),
          parseRegionKind(json_doc.value("region_kind", std::string{"Exterior"}))),
      region_json(json_doc), region_folder(std::move(folder))
{
    // Parse enemy_spawns. Each entry's id, archetype, and pos are
    // REQUIRED; we throw on missing so a typo'd JSON file fails
    // boot rather than silently spawning nothing (the same silent-
    // fallback bug class that bit us in the Scene->Region rename;
    // see [[feedback_check_clip_classification_first]]+
    // region_schema_test.cpp).
    if (json_doc.contains("enemy_spawns") && !json_doc["enemy_spawns"].is_null())
    {
        const auto& arr = json_doc.at("enemy_spawns");
        if (!arr.is_array())
            throw std::runtime_error("enemy_spawns must be an array");
        for (const auto& s : arr)
        {
            selva::gameplay::EnemySpawnDecl d;
            d.id = s.at("id").get<std::string>();
            d.archetype = s.at("archetype").get<std::string>();
            const auto& pos_arr = s.at("pos");
            if (!pos_arr.is_array() || pos_arr.size() < 3)
                throw std::runtime_error("enemy_spawns[].pos must be [x, y, z]");
            const float px = pos_arr[0].get<float>();
            const float pz = pos_arr[2].get<float>();
            // pos[1] is either a numeric Y OR the string sentinel
            // "auto_terrain" (resolves to groundHeight(x,z) at spawn).
            float py = 0.0f;
            if (pos_arr[1].is_string())
            {
                if (pos_arr[1].get<std::string>() == "auto_terrain")
                    d.pos_y_auto_terrain = true;
                else
                    throw std::runtime_error("enemy_spawns[].pos[1] string must be \"auto_terrain\"");
            }
            else
            {
                py = pos_arr[1].get<float>();
            }
            d.pos = glm::vec3(px, py, pz);
            d.yaw = s.value("yaw", 0.0f);
            d.permanent_on_death = s.value("permanent_on_death", false);
            if (s.contains("patrol_path") && s["patrol_path"].is_array())
            {
                for (const auto& wp : s["patrol_path"])
                {
                    if (!wp.is_array() || wp.size() < 3)
                        continue;
                    d.patrol_path.emplace_back(wp[0].get<float>(), wp[1].get<float>(),
                                               wp[2].get<float>());
                }
            }
            enemy_spawn_decls.push_back(std::move(d));
        }
    }

    // Parse terrain_modifiers in the constructor (not commitPrepared)
    // so registration can happen at boot BEFORE initTerrain. The
    // global terrain mesh builder samples per-vertex Y through the
    // modifier registry; modifiers registered AFTER initTerrain don't
    // affect the already-built mesh. We split JsonRegion's load into
    // two phases: registerModifiers() runs before initTerrain, then
    // preloadAssets() runs after (it needs the terrain mesh to exist).
    //
    // Owned strings (debug_name + terrain_region) live in
    // parsed_strings; TerrainModifier stores const char* into them.
    // unique_ptr<string> ensures push_back never invalidates the
    // c_str() pointers held by parsed_modifiers.
    if (region_json.contains("terrain_modifiers") &&
        !region_json["terrain_modifiers"].is_null())
    {
        const auto& mods_json = region_json.at("terrain_modifiers");
        if (!mods_json.is_array())
            throw std::runtime_error("terrain_modifiers must be an array");
        parsed_modifiers.reserve(mods_json.size());
        // Reserve generously: 2 owned strings per modifier (debug_name + terrain_region).
        parsed_strings.reserve(mods_json.size() * 2);
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
            mod.blend_pad_neg_x = m.value("blend_pad_neg_x", -1.0f);
            mod.blend_pad_pos_x = m.value("blend_pad_pos_x", -1.0f);
            mod.blend_pad_neg_z = m.value("blend_pad_neg_z", -1.0f);
            mod.blend_pad_pos_z = m.value("blend_pad_pos_z", -1.0f);
            // debug_name + terrain_region: own the strings here so their
            // c_str() outlives the registry's pointer copy.
            if (m.contains("debug_name") && m["debug_name"].is_string())
            {
                parsed_strings.push_back(
                    std::make_unique<std::string>(m["debug_name"].get<std::string>()));
                mod.debug_name = parsed_strings.back()->c_str();
            }
            // terrain_region: which terrain region (from terrain/config.json)
            // this modifier targets. nullptr / unset = global (applies to
            // any terrain region whose XZ AABB contains the query). Setting
            // it scopes the modifier to that one terrain region.
            if (m.contains("terrain_region") && m["terrain_region"].is_string())
            {
                parsed_strings.push_back(
                    std::make_unique<std::string>(m["terrain_region"].get<std::string>()));
                mod.region_name = parsed_strings.back()->c_str();
            }
            parsed_modifiers.push_back(mod);
        }
    }

    // Parse ai_block_volumes. Each volume becomes an AABB that AI
    // actors from foreign regions can't enter. Owner is implicitly
    // this region. See AiBarriers.h doctrine.
    if (region_json.contains("ai_block_volumes") && !region_json["ai_block_volumes"].is_null())
    {
        const auto& arr = region_json.at("ai_block_volumes");
        if (!arr.is_array())
            throw std::runtime_error("ai_block_volumes must be an array");
        for (const auto& v : arr)
        {
            selva::gameplay::AiBlockVolume vol;
            vol.center = parseVec3(v.at("center"));
            vol.half_extents = parseVec3(v.at("half_extents"));
            vol.owner_region_id = regionId();
            vol.debug_name = v.value("debug_name", std::string{});
            parsed_ai_block_volumes.push_back(std::move(vol));
        }
    }
}

void JsonRegion::registerModifiers()
{
    for (const auto& mod : parsed_modifiers)
        engine::world::registerTerrainModifier(mod);
    if (!parsed_modifiers.empty())
        std::fprintf(stderr, "[json-region '%s'] registered %zu terrain modifier(s)\n",
                     regionId().c_str(), parsed_modifiers.size());
    for (const auto& vol : parsed_ai_block_volumes)
        selva::gameplay::registerAiBlockVolume(vol);
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

    // Terrain modifiers are parsed in the constructor + registered
    // by registerModifiers() (called at boot BEFORE initTerrain so
    // the mesh builder sees them). Not touched here.

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
                 regionId().c_str(), loaded_meshes.size(), parsed_modifiers.size(),
                 trigs_json.size());
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
