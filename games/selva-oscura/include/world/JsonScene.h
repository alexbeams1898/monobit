#pragma once

#include "world/AsyncSceneLoader.h"
#include "world/StaticMeshAssets.h"
#include "world/TerrainModifiers.h"

#include <glm/vec3.hpp>
#include <nlohmann/json.hpp>

#include <memory>
#include <string>
#include <vector>

namespace selva::world
{

// One scene loaded from a scene.json file. The schema is documented at
// games/selva-oscura/assets/scenes/SCHEMA.md.
//
// Async-capable: prepareAsync() reads .glb files + builds CPU vertex
// arrays off the main thread; commitPrepared() inserts Jolt bodies on
// the main thread.
//
// Scene-local terrain (heightmap) is handled by the SurfaceScene
// instance because the existing engine terrain module is a global
// singleton today. Once a second terrain-bearing scene appears we'll
// pull that into a scene-local state too. The chapel_interior and
// acheron scenes don't have terrain, so this isn't blocking.
class JsonScene : public engine::world::AsyncCapableScene
{
  public:
    // Construct from already-parsed JSON + the folder the scene.json
    // lives in (used for asset path resolution).
    JsonScene(const nlohmann::json& scene_json, std::string scene_folder);

    // Pre-load all .glb meshes referenced by the JSON. Called ONCE
    // at boot (after scene registration). File I/O + GL upload +
    // CPU vertex arrays — all heavy work. Body insertion does NOT
    // happen here; that's per-activation.
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
        return mPreloaded;
    }

    // prepareAsync now no-ops (work moved to preloadAssets at boot).
    void prepareAsync() override
    {
    }

    // commitPrepared = main-thread per-activation work: insert Jolt
    // static trimesh bodies from already-loaded CPU positions,
    // register terrain modifiers (no-op for non-terrain scenes),
    // register triggers. Fast (sub-ms for 444 bodies).
    void commitPrepared(engine::world::SceneActivationContext& ctx) override;

    // Scene-local cleanup at deactivation. Bodies are automatic
    // (engine tracks via context). GL resources STAY allocated
    // (resident-all-scenes); freed at game shutdown via freeAssets.
    void onDeactivate() override;

    // Free GL/CPU resources at game shutdown. Hooked via the
    // engine Scene::onShutdown override.
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

  private:
    nlohmann::json mJson;
    std::string mFolder;

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
    };
    std::vector<std::unique_ptr<LoadedMesh>> mMeshes;
    // Preloaded shape handles for terrain regions. Built in
    // preloadAssets, reused on each activation. Index aligns with
    // selva::world::terrainRegion(i).
    std::vector<engine::physics::ShapeHandle> mTerrainShapeHandles;
    bool mPreloaded = false;
};

} // namespace selva::world
