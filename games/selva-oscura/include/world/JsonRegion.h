#pragma once

#include "gameplay/Enemies.h"
#include "gameplay/PropArchetype.h"
#include "interact/Interaction.h"
#include "world/AsyncRegionLoader.h"
#include "world/Door.h"
#include "world/StaticMeshAssets.h"
#include "world/TerrainModifiers.h"
#include "world/Territory.h"

#include <glm/vec3.hpp>
#include <nlohmann/json.hpp>

#include <memory>
#include <string>
#include <vector>

namespace selva::world
{

// One scene loaded from a region.json file. The schema is documented at
// games/selva-oscura/assets/regions/SCHEMA.md.
//
// Async-capable: prepareAsync() reads .glb files + builds CPU vertex
// arrays off the main thread; commitPrepared() inserts Jolt bodies on
// the main thread.
//
// Region-local terrain (heightmap) is handled by the SurfaceRegion
// instance because the existing engine terrain module is a global
// singleton today. Once a second terrain-bearing scene appears we'll
// pull that into a scene-local state too.
class JsonRegion : public engine::world::AsyncCapableRegion
{
  public:
    // Construct from already-parsed JSON + the folder the region.json
    // lives in (used for asset path resolution).
    JsonRegion(const nlohmann::json& json_doc, std::string folder);

    // Phase 1 of boot: register this region's authored terrain
    // modifiers into the global TerrainModifier registry. Called
    // BEFORE initTerrain() so the terrain mesh builder sees them
    // when sampling per-vertex Y. Idempotent — calling twice
    // double-registers; loadAllRegionsRegister calls exactly once per region.
    //
    // Modifiers are parsed from the region.json's terrain_modifiers
    // array in the JsonRegion constructor (owning their strings for
    // stable lifetime); this method just hands them to the global
    // registry.
    void registerModifiers();

    // Phase 2 of boot: pre-load all .glb meshes referenced by the
    // JSON + build Jolt shapes for them, AND build Jolt shapes for
    // the terrain mesh(es) this region uses. Called AFTER
    // initTerrain() so the terrain CPU vertex arrays exist. Body
    // INSERTION does NOT happen here; that's per-activation.
    //
    // Resident-all-scenes architecture: every scene's meshes stay
    // loaded in GPU memory + CPU vertex arrays for the entire game
    // session. Activation/deactivation is JUST Jolt body insertion/
    // removal — sub-millisecond. No load stalls during transitions.
    // Trade: VRAM footprint scales with total scene count. For
    // Selva's scale (10s of scenes max) this is trivial.
    void preloadAssets();

    // True once preloadAssets has run. Used by lazy callers to
    // detect whether the heavy boot-time work has happened yet.
    bool isPreloaded() const
    {
        return is_preloaded;
    }

    // commitPrepared = main-thread per-activation work: insert Jolt
    // static trimesh bodies from already-loaded CPU positions,
    // register terrain modifiers (no-op for non-terrain scenes),
    // register triggers. Fast (sub-ms for 444 bodies).
    void commitPrepared(engine::world::RegionActivationContext& ctx) override;

    // Region-local cleanup at deactivation. Bodies are automatic
    // (engine tracks via context). GL resources STAY allocated
    // (resident-all-scenes); freed at game shutdown via freeAssets.
    void onDeactivate() override;

    // Free GL/CPU resources at game shutdown. Hooked via the
    // engine Region::onShutdown override.
    void freeAssets();

    void onShutdown() override
    {
        freeAssets();
    }

    // Per-frame COLOR draw hook. Renders this scene's static meshes
    // through the scene color shader. Sets setSceneModel(identity)
    // because positions are baked in world space.
    void renderMeshes() const;

    // Per-frame DEPTH draw hook. Same primitives, but the caller
    // has bound the depth-pass shader + set its model matrix
    // (identity for scene-owned meshes). Just binds VAOs + draws,
    // no color/tint setup.
    void renderMeshesDepth() const;

    // Parsed enemy spawn declarations from the region.json's
    // "enemy_spawns" array. Populated at construction. Spawn timing
    // (when the actor pool gets populated from these decls) is
    // separate -- see selva::world::spawnActiveRegionEnemies(), which
    // requires the archetype registry + behavior trees to be loaded
    // first.
    const std::vector<selva::gameplay::EnemySpawnDecl>& enemySpawnDecls() const
    {
        return enemy_spawn_decls;
    }

    // Parsed prop declarations from the region.json `props[]` array.
    // Populated at construction; consumed at region commit by the
    // prop spawn funnel (which resolves each decl's archetype and
    // attaches components / generates a cylinder collider for
    // physical props).
    const std::vector<selva::gameplay::PropDecl>& propDecls() const
    {
        return prop_decls;
    }

    // Parsed prop_scatter rules from the region.json `prop_scatter[]`
    // array. Procgen siblings of propDecls(); evaluated at region
    // commit (the producer emits PropDecls into the same downstream
    // funnel).
    const std::vector<selva::gameplay::PropScatterRule>& propScatterRules() const
    {
        return prop_scatter_rules;
    }

  private:
    // Constructor delegates to these per-section parsers so the ctor
    // itself stays a flat list of calls and lizard doesn't fail on
    // its cyclomatic complexity.
    void parseActorSpawns(const nlohmann::json& json_doc);
    void parseProps(const nlohmann::json& json_doc);
    void parseDoors(const nlohmann::json& json_doc);
    void parseTerrainModifiers();
    void parseTerritory();
    void parseHazardZones();

    // preloadAssets() delegates to these per-section helpers so it
    // stays a flat call list and lizard doesn't fail on its
    // cyclomatic complexity / length.
    void preloadStaticMeshEntry(const nlohmann::json& m);
    void preloadTerrainShapes();
    // commitPrepared helpers (keeps the orchestrator's CCN under the
    // lizard threshold by extracting each phase).
    void registerStaticMeshExamines();

    nlohmann::json region_json;
    std::string region_folder;

    struct LoadedMesh
    {
        std::string path;
        glm::vec3 world_origin{0.0f};
        engine::physics::SurfaceTag tag = engine::physics::SurfaceTag::Architecture;
        std::string debug_name;
        StaticMesh mesh; // CPU + GPU
        bool loaded = false;
        // One ShapeHandle per primitive. Built once in preloadAssets
        // so commitPrepared can just add bodies (sub-ms) instead of
        // rebuilding BVHs every activation.
        std::vector<engine::physics::ShapeHandle> shape_handles;
        // Optional Examine interactable. Non-empty examine_text =
        // pressing E within interact_range_meters of examine_anchor
        // pops the text in Grimoire register. Used for cosmological
        // waypoints (Acheron pile, vestigia, future shrines). Empty
        // examine_text = no interactable. Field name matches the
        // archetype-side EnemyArchetype::interact_range_meters.
        std::string examine_text;
        std::string examine_label;
        // Language-map keys for tier-gated player-facing strings.
        // examine_label_key: resolves to the "[E] Examine ..." prompt
        // noun. examine_text_key: resolves to the prose body that
        // shows when E is pressed. When EITHER is set, it wins over
        // the literal examine_label / examine_text field below
        // (which serve as last-resort fallbacks for content that
        // genuinely has no tier-2 reveal).
        std::string examine_label_key;
        std::string examine_text_key;
        // Multi-tier examine text keys. When non-empty, indexed by the
        // player's current examine count for this mesh (capped at the
        // last index). On first examine count=0 -> [0], second
        // count=1 -> [1], etc. When empty, falls back to the single
        // examine_text_key / examine_text. Lets a mesh surface
        // different prose on re-examine ("you notice on a closer
        // look..."), and pairs with examined:<subject>:<N> insight
        // flags so each tier can drive its own insight node.
        std::vector<std::string> examine_text_keys;
        glm::vec3 examine_anchor{0.0f};
        float interact_range_meters = 2.5f;
    };
    std::vector<std::unique_ptr<LoadedMesh>> loaded_meshes;
    // Interactable ids registered at commitPrepared for any
    // loaded_mesh with non-empty examine_text. Unregistered on
    // onDeactivate so a region transition doesn't leak examines.
    std::vector<selva::interact::Id> mesh_interactable_ids;
    // Preloaded shape handles for terrain regions. Built in
    // preloadAssets, reused on each activation. Index aligns with
    // selva::world::terrainRegion(i).
    std::vector<engine::physics::ShapeHandle> terrain_shapes;
    bool is_preloaded = false;

    // Parsed in constructor from the region_json's "enemy_spawns"
    // array. Authoritative source of truth for which enemies belong
    // to this region.
    std::vector<selva::gameplay::EnemySpawnDecl> enemy_spawn_decls;

    // Parsed in constructor from the region_json's "props" array.
    // Authoritative source of truth for which props belong to this
    // region. Consumed at commitPrepared by the prop spawn funnel.
    std::vector<selva::gameplay::PropDecl> prop_decls;

    // Parsed in constructor from the region_json's "prop_scatter"
    // array. Each rule is evaluated by the scatter producer at
    // region commit, emitting PropDecls into the runtime as side
    // effects (the producer pushes directly to the CollisionRegion).
    std::vector<selva::gameplay::PropScatterRule> prop_scatter_rules;

    // Parsed-in-constructor terrain modifiers. We own these for the
    // region's lifetime (which is the program's lifetime per the
    // resident-all-scenes architecture). The TerrainModifier struct
    // stores const char* for debug_name / region_name; those pointers
    // refer into parsed_strings below for stable lifetime. Strings go
    // into one append-only vector so member growth doesn't invalidate
    // .c_str() pointers we already handed to the modifier registry.
    std::vector<engine::world::TerrainModifier> parsed_modifiers;
    // Append-only string pool. Reserved up front to size of the
    // parsed_modifiers list × 2 fields per modifier so push_back
    // never reallocates. Strings stored as std::string for ownership;
    // .c_str() is handed to the modifier registry via parsed_modifiers.
    std::vector<std::unique_ptr<std::string>> parsed_strings;

    // Parsed-in-constructor territory volumes (`territory` array in
    // region.json). Define which world-space positions belong to this
    // region's law-domain. Each volume's owner_region_id is set
    // automatically to this region's id. registerModifiers() hands
    // them to the global engine::world territory registry at boot.
    std::vector<engine::world::Territory> parsed_territory;

    // Door instances declared in region.json "doors":[]. Registered
    // with the world::Door system at commitPrepared time. See
    // [[world/Door.h]] for the lifecycle.
    std::vector<selva::world::DoorDecl> parsed_doors;
};

} // namespace selva::world
