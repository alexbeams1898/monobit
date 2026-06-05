#include "world/JsonRegion.h"

#include "debug/Flags.h"
#include "hazard/HazardZones.h"
#include "interact/Interaction.h"
#include "physics/PhysicsWorld.h"
#include "render/RegionShaders.h"
#include "text/Examine.h"
#include "text/TextPresentation.h"
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

namespace
{
glm::vec3 parsePosAllowAutoTerrain(const nlohmann::json& pos_arr, bool& out_auto_terrain,
                                   const char* schema_label)
{
    if (!pos_arr.is_array() || pos_arr.size() < 3)
        throw std::runtime_error(std::string(schema_label) + " must be [x, y, z]");
    const float x = pos_arr[0].get<float>();
    const float z = pos_arr[2].get<float>();
    float y = 0.0f;
    if (pos_arr[1].is_string())
    {
        if (pos_arr[1].get<std::string>() == "auto_terrain")
            out_auto_terrain = true;
        else
            throw std::runtime_error(std::string(schema_label) +
                                     "[1] string must be \"auto_terrain\"");
    }
    else
    {
        y = pos_arr[1].get<float>();
    }
    return glm::vec3(x, y, z);
}

void parsePostFlagPositions(const nlohmann::json& arr, selva::gameplay::EnemySpawnDecl& d)
{
    for (const auto& fp : arr)
    {
        if (!fp.is_object() || !fp.contains("flag") || !fp.contains("pos"))
            continue;
        selva::gameplay::EnemySpawnDecl::FlagPosition entry;
        entry.flag = fp.at("flag").get<std::string>();
        const auto& fp_pos = fp.at("pos");
        if (!fp_pos.is_array() || fp_pos.size() < 3)
            continue;
        entry.pos =
            parsePosAllowAutoTerrain(fp_pos, entry.pos_y_auto_terrain, "post_flag_positions[].pos");
        entry.yaw = fp.value("yaw", 0.0f);
        d.post_flag_positions.push_back(std::move(entry));
    }
}

void parsePatrolPath(const nlohmann::json& s, selva::gameplay::EnemySpawnDecl& d)
{
    if (!s.contains("patrol_path") || !s["patrol_path"].is_array())
        return;
    for (const auto& wp : s["patrol_path"])
        if (wp.is_array() && wp.size() >= 3)
            d.patrol_path.emplace_back(wp[0].get<float>(), wp[1].get<float>(), wp[2].get<float>());
}

void parseScriptedTarget(const nlohmann::json& s, selva::gameplay::EnemySpawnDecl& d)
{
    if (!s.contains("scripted_target_pos") || !s["scripted_target_pos"].is_array() ||
        s["scripted_target_pos"].size() < 3)
        return;
    d.scripted_target_pos = glm::vec3(s["scripted_target_pos"][0].get<float>(),
                                      s["scripted_target_pos"][1].get<float>(),
                                      s["scripted_target_pos"][2].get<float>());
}

void parseArenaAndPostFlags(const nlohmann::json& s, selva::gameplay::EnemySpawnDecl& d)
{
    d.spawn_trigger_id = s.value("spawn_trigger_id", std::string{});
    d.engage_trigger_id = s.value("engage_trigger_id", std::string{});
    if (s.contains("arena_center") && s["arena_center"].is_array() && s["arena_center"].size() >= 3)
        d.arena_center = parseVec3(s["arena_center"]);
    if (s.contains("arena_half_extents") && s["arena_half_extents"].is_array() &&
        s["arena_half_extents"].size() >= 3)
        d.arena_half_extents = parseVec3(s["arena_half_extents"]);
    if (s.contains("post_flag_positions") && s["post_flag_positions"].is_array())
        parsePostFlagPositions(s["post_flag_positions"], d);
}

selva::gameplay::EnemySpawnDecl parseActorSpawn(const nlohmann::json& s)
{
    selva::gameplay::EnemySpawnDecl d;
    d.id = s.at("id").get<std::string>();
    d.archetype = s.at("archetype").get<std::string>();
    d.pos = parsePosAllowAutoTerrain(s.at("pos"), d.pos_y_auto_terrain, "actor_spawns[].pos");
    d.yaw = s.value("yaw", 0.0f);
    d.permanent_on_death = s.value("permanent_on_death", false);
    d.scripted_stop_range = s.value("scripted_stop_range", 0.5f);
    parsePatrolPath(s, d);
    parseScriptedTarget(s, d);
    parseArenaAndPostFlags(s, d);
    return d;
}

selva::world::DoorDecl parseDoor(const nlohmann::json& s)
{
    selva::world::DoorDecl d;
    d.id = s.at("id").get<std::string>();
    const auto& pos_arr = s.at("pos");
    if (!pos_arr.is_array() || pos_arr.size() < 3)
        throw std::runtime_error("doors[].pos must be [x, y, z]");
    d.pos = glm::vec3(pos_arr[0].get<float>(), pos_arr[1].get<float>(), pos_arr[2].get<float>());
    d.yaw = s.value("yaw", 0.0f);
    if (s.contains("hinge_offset") && s["hinge_offset"].is_array() && s["hinge_offset"].size() >= 3)
        d.hinge_offset = parseVec3(s["hinge_offset"]);
    const std::string axis_str = s.value("hinge_axis", std::string("Y"));
    d.hinge_axis = axis_str.empty() ? 'Y' : axis_str[0];
    d.open_angle_radians = s.value("open_angle_degrees", 90.0f) * 3.14159265f / 180.0f;
    d.open_animation_seconds = s.value("open_animation_seconds", 1.0f);
    d.mesh_path = s.value("mesh", std::string{});
    d.player_interactable = s.value("player_interactable", true);
    d.persistent = s.value("persistent", true);
    d.initial_state = selva::world::parseDoorState(s.value("initial_state", std::string("Closed")));
    return d;
}

engine::world::TerrainModifier
parseTerrainModifier(const nlohmann::json& m,
                     std::vector<std::unique_ptr<std::string>>& owned_strings)
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
    if (m.contains("debug_name") && m["debug_name"].is_string())
    {
        owned_strings.push_back(std::make_unique<std::string>(m["debug_name"].get<std::string>()));
        mod.debug_name = owned_strings.back()->c_str();
    }
    if (m.contains("terrain_region") && m["terrain_region"].is_string())
    {
        owned_strings.push_back(
            std::make_unique<std::string>(m["terrain_region"].get<std::string>()));
        mod.region_name = owned_strings.back()->c_str();
    }
    return mod;
}
} // namespace

void JsonRegion::parseActorSpawns(const nlohmann::json& json_doc)
{
    // Accept both `actor_spawns` (current) and `enemy_spawns` (legacy
    // alias). Required fields: id, archetype, pos. Throws on malformed
    // input so a typo'd JSON fails at boot, not silently at runtime.
    const char* spawns_key = nullptr;
    if (json_doc.contains("actor_spawns") && !json_doc["actor_spawns"].is_null())
        spawns_key = "actor_spawns";
    else if (json_doc.contains("enemy_spawns") && !json_doc["enemy_spawns"].is_null())
        spawns_key = "enemy_spawns";
    if (spawns_key == nullptr)
        return;
    const auto& arr = json_doc.at(spawns_key);
    if (!arr.is_array())
        throw std::runtime_error(std::string(spawns_key) + " must be an array");
    for (const auto& s : arr)
        enemy_spawn_decls.push_back(parseActorSpawn(s));
}

void JsonRegion::parseDoors(const nlohmann::json& json_doc)
{
    if (!json_doc.contains("doors") || json_doc["doors"].is_null())
        return;
    const auto& arr = json_doc.at("doors");
    if (!arr.is_array())
        throw std::runtime_error("doors must be an array");
    for (const auto& s : arr)
        parsed_doors.push_back(parseDoor(s));
}

void JsonRegion::parseTerrainModifiers()
{
    // Parsed in the ctor (not commitPrepared) so registration happens
    // at boot BEFORE initTerrain -- the global mesh builder samples
    // per-vertex Y through the modifier registry. Strings the registry
    // borrows by pointer live in `parsed_strings`.
    if (!region_json.contains("terrain_modifiers") || region_json["terrain_modifiers"].is_null())
        return;
    const auto& mods_json = region_json.at("terrain_modifiers");
    if (!mods_json.is_array())
        throw std::runtime_error("terrain_modifiers must be an array");
    parsed_modifiers.reserve(mods_json.size());
    parsed_strings.reserve(mods_json.size() * 2);
    for (const auto& m : mods_json)
        parsed_modifiers.push_back(parseTerrainModifier(m, parsed_strings));
}

void JsonRegion::parseAiBlockVolumes()
{
    if (!region_json.contains("ai_block_volumes") || region_json["ai_block_volumes"].is_null())
        return;
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

void JsonRegion::parseHazardZones()
{
    if (!region_json.contains("hazard_zones") || region_json["hazard_zones"].is_null())
        return;
    const auto& arr = region_json.at("hazard_zones");
    if (!arr.is_array())
        throw std::runtime_error("hazard_zones must be an array");
    for (const auto& v : arr)
    {
        selva::hazard::HazardZone zone;
        zone.kind = v.at("kind").get<std::string>();
        zone.center = parseVec3(v.at("center"));
        zone.half_extents = parseVec3(v.at("half_extents"));
        selva::hazard::registerZone(zone);
    }
}

JsonRegion::JsonRegion(const nlohmann::json& json_doc, std::string folder)
    : engine::world::AsyncCapableRegion(
          // region_id is required; .at() throws so a nameless region
          // fails boot rather than silently breaking findRegionId.
          json_doc.at("region_id").get<std::string>(), json_doc.value("debug_name", std::string{}),
          parseRegionKind(json_doc.value("region_kind", std::string{"Exterior"}))),
      region_json(json_doc), region_folder(std::move(folder))
{
    parseActorSpawns(json_doc);
    parseDoors(json_doc);
    parseTerrainModifiers();
    parseAiBlockVolumes();
    parseHazardZones();
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

namespace
{
void logMeshWorldBounds(const StaticMesh& mesh)
{
    glm::vec3 bmin(std::numeric_limits<float>::infinity());
    glm::vec3 bmax(-std::numeric_limits<float>::infinity());
    std::size_t total_verts = 0;
    for (const auto& prim : mesh.primitives)
    {
        for (const auto& p : prim.cpu_positions)
        {
            bmin = glm::min(bmin, p);
            bmax = glm::max(bmax, p);
        }
        total_verts += prim.cpu_positions.size();
    }
    if (total_verts == 0)
        return;
    std::fprintf(stderr,
                 "  world-bounds: x=[%.2f, %.2f] y=[%.2f, %.2f] z=[%.2f, %.2f] "
                 "center=(%.2f, %.2f, %.2f) verts=%zu\n",
                 bmin.x, bmax.x, bmin.y, bmax.y, bmin.z, bmax.z, (bmin.x + bmax.x) * 0.5f,
                 (bmin.y + bmax.y) * 0.5f, (bmin.z + bmax.z) * 0.5f, total_verts);
}
} // namespace

void JsonRegion::preloadStaticMeshEntry(const nlohmann::json& m)
{
    auto lm = std::make_unique<LoadedMesh>();
    lm->path = m.value("path", std::string{});
    lm->world_origin = parseVec3(m.value("world_origin", nlohmann::json::array()));
    lm->tag = parseSurfaceTag(m.value("surface_tag", std::string{"Architecture"}));
    lm->debug_name = m.value("debug_name", std::string{});
    // Optional Examine interactable: pressing E within range pops the
    // text in Grimoire register. Anchor defaults to world_origin if
    // examine_anchor isn't specified (most props -- the pile, vestigia
    // -- want the prompt centered on the mesh origin).
    lm->examine_text = m.value("examine_text", std::string{});
    // Default label to debug_name so an author who declares examine_text
    // without a label gets a sane prompt instead of an empty one.
    lm->examine_label = m.value("examine_label", lm->debug_name);
    if (m.contains("examine_anchor") && m["examine_anchor"].is_array() &&
        m["examine_anchor"].size() >= 3)
        lm->examine_anchor = parseVec3(m["examine_anchor"]);
    else
        lm->examine_anchor = lm->world_origin;
    // Match the field name used by archetype-side interactables
    // (EnemyArchetype::interact_range_meters) so authors don't guess.
    lm->interact_range_meters = m.value("interact_range_meters", 2.5f);
    if (!loadStaticMesh(lm->path.c_str(), lm->world_origin, lm->mesh))
    {
        std::fprintf(stderr, "[json-region '%s'] failed to load mesh: %s\n", regionId().c_str(),
                     lm->path.c_str());
        return;
    }
    lm->loaded = true;
    // One ShapeHandle per primitive. Slots stay aligned to mesh.primitives
    // (kInvalidShape for skipped/visual-only prims) so commitPrepared can
    // index either array safely.
    lm->shape_handles.reserve(lm->mesh.primitives.size());
    for (const auto& prim : lm->mesh.primitives)
    {
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
    logMeshWorldBounds(lm->mesh);
    loaded_meshes.push_back(std::move(lm));
}

void JsonRegion::preloadTerrainShapes()
{
    if (!region_json.contains("terrain") || region_json["terrain"].is_null())
        return;
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

void JsonRegion::preloadAssets()
{
    if (is_preloaded)
        return; // idempotent — lazy callers can call freely
    std::fprintf(stderr, "[json-region '%s'] preloadAssets START\n", regionId().c_str());
    // File I/O + GL upload + Jolt SHAPE construction happen HERE,
    // once at boot. Per-activation commit reuses preloaded shapes to
    // insert bodies in O(1) -- no per-transition BVH rebuilds. (Terrain
    // alone is 73k tris and rebuilding its MeshShape on every
    // region-exit costs ~400ms in Debug.)
    const auto& meshes_json = region_json.value("static_meshes", nlohmann::json::array());
    for (const auto& m : meshes_json)
        preloadStaticMeshEntry(m);
    preloadTerrainShapes();

    std::fprintf(stderr, "[json-region '%s'] preloadAssets END (%zu meshes, %zu terrain shapes)\n",
                 regionId().c_str(), loaded_meshes.size(), terrain_shapes.size());
    is_preloaded = true;
}

// Per-mesh examine_text in region.json registers an Examine prompt
// at the mesh's examine_anchor. Used for cosmological waypoints
// (Acheron pile, vestigia, future shrines). Ids tracked in
// mesh_interactable_ids; cleaned up on deactivate.
void JsonRegion::registerStaticMeshExamines()
{
    for (const auto& lm : loaded_meshes)
    {
        if (lm->examine_text.empty())
            continue;
        selva::interact::Decl idecl;
        idecl.kind = selva::interact::Kind::Examine;
        const glm::vec3 anchor = lm->examine_anchor;
        idecl.position = [anchor]() { return anchor; };
        idecl.range_meters = lm->interact_range_meters;
        idecl.label = lm->examine_label;
        const std::string text = lm->examine_text;
        idecl.on_interact = [text]() { selva::text::beginExamine(text); };
        idecl.available = []() { return !selva::text::active(); };
        const selva::interact::Id id = selva::interact::registerInteractable(std::move(idecl));
        mesh_interactable_ids.push_back(id);
    }
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

    registerStaticMeshExamines();

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
        // Action: defaults to "RegionTransition" (existing semantics);
        // "Custom" routes game-side via action_payload (see boss
        // backend per docs/design/ideas/boss_backend.md).
        const std::string action_str = t.value("action", std::string{"RegionTransition"});
        if (action_str == "Custom")
            trig.action = engine::world::TriggerAction::Custom;
        else
            trig.action = engine::world::TriggerAction::RegionTransition;
        trig.action_payload = t.value("action_payload", std::string{});
        // RegionTransition fields (ignored for Custom):
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
        // Only warn about missing target_region for RegionTransition
        // triggers -- Custom triggers don't need a target.
        if (trig.action == engine::world::TriggerAction::RegionTransition &&
            trig.target == engine::world::kInvalidRegion)
        {
            std::fprintf(stderr, "[json-region '%s'] trigger '%s' targets unknown region '%s'\n",
                         regionId().c_str(), trig.id.c_str(), target_region_id.c_str());
        }
    }
    // Register doors with the world-Door system (creates colliders +
    // resolves persisted state from active profile).
    selva::world::registerDoorsForRegion(parsed_doors);
    std::fprintf(
        stderr,
        "[json-region '%s'] commitPrepared END (%zu meshes, %zu mods, %zu triggers, %zu doors)\n",
        regionId().c_str(), loaded_meshes.size(), parsed_modifiers.size(), trigs_json.size(),
        parsed_doors.size());
}

void JsonRegion::onDeactivate()
{
    // Resident-all-scenes: GL resources stay allocated across the
    // game session so re-activation is sub-ms. They're freed at
    // game shutdown via freeAssets(). Body cleanup is automatic
    // via the engine's owned-body tracking.
    //
    // Per-mesh Examine interactables MUST be unregistered: their
    // closures captured the mesh's anchor pos + text by value, so
    // the registry would otherwise keep them alive across regions
    // and the player would press E to read a pile of larvae from
    // miles away.
    for (const auto id : mesh_interactable_ids)
        selva::interact::unregisterInteractable(id);
    mesh_interactable_ids.clear();
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
    const bool debug_id = selva::debug::flags().primitive_id_colors;
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
