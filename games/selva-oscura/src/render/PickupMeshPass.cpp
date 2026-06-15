#include "render/PickupMeshPass.h"

#include "items/ItemRegistry.h"
#include "loot/Pickups.h"
#include "render/RegionShaders.h"
#include "world/StaticMeshAssets.h"

#include <glm/gtc/matrix_transform.hpp>

#include <cstdio>
#include <string>
#include <unordered_map>

#include <glad/glad.h>

namespace selva::render
{

namespace
{

// Process-wide cache of loaded pickup meshes keyed by item config_path
// (so all bark scraps share one mesh). First spawn of an item loads +
// uploads; every subsequent spawn hits the cache.
std::unordered_map<std::string, selva::world::StaticMesh>& meshCache()
{
    static std::unordered_map<std::string, selva::world::StaticMesh> cache;
    return cache;
}

// First-load failure memo so we don't spam logs on a per-frame retry
// of a broken path. Mirrors EquippedWeapon's pattern.
std::unordered_map<std::string, bool>& loadFailureMemo()
{
    static std::unordered_map<std::string, bool> memo;
    return memo;
}

// Resolve the cached mesh for an ItemDef's world_mesh path. Loads on
// first touch. Returns nullptr if def has no world_mesh, load failed,
// or load was previously memoed as failed.
const selva::world::StaticMesh* resolveMesh(const engine::ecs::ItemDef& def)
{
    if (def.world_mesh.empty())
        return nullptr;

    auto& cache = meshCache();
    if (auto it = cache.find(def.world_mesh); it != cache.end())
        return &it->second;

    if (loadFailureMemo()[def.world_mesh])
        return nullptr;

    selva::world::StaticMesh mesh;
    const bool ok = selva::world::loadStaticMesh(def.world_mesh.c_str(), glm::vec3(0.0f), mesh);
    if (!ok)
    {
        std::fprintf(stderr,
                     "[pickup-mesh] failed to load '%s' for '%s'; will not retry this session\n",
                     def.world_mesh.c_str(), def.config_path.c_str());
        std::fflush(stderr);
        loadFailureMemo()[def.world_mesh] = true;
        return nullptr;
    }
    std::fprintf(stderr, "[pickup-mesh] loaded '%s' (%zu primitives)\n", def.world_mesh.c_str(),
                 mesh.primitives.size());
    std::fflush(stderr);

    auto [inserted, _] = cache.emplace(def.world_mesh, std::move(mesh));
    return &inserted->second;
}

} // namespace

void clearPickupMeshCache()
{
    for (auto& [_, mesh] : meshCache())
        selva::world::freeStaticMeshGLResources(mesh);
    meshCache().clear();
    loadFailureMemo().clear();
}

void renderPickupMeshes()
{
    const auto& pickups = selva::loot::allPickups();
    if (pickups.empty())
        return;

    const auto& items = selva::items::itemRegistry();

    for (const auto& p : pickups)
    {
        const engine::ecs::ItemDef* def = items.find(p.item.config_path);
        if (def == nullptr)
            continue;
        const selva::world::StaticMesh* mesh = resolveMesh(*def);
        if (mesh == nullptr || mesh->primitives.empty())
            continue;

        const glm::vec3 pos = selva::loot::livePickupPos(p);
        // Translate to pickup pos, then yaw about Y for per-pickup
        // variety (gather nodes commit a random yaw at spawn time;
        // corpse-bound and authored pickups default to 0).
        glm::mat4 model(1.0f);
        model = glm::translate(model, pos);
        if (p.world_yaw != 0.0f)
            model = glm::rotate(model, p.world_yaw, glm::vec3(0.0f, 1.0f, 0.0f));
        selva::render::setSceneModel(model);

        for (const auto& prim : mesh->primitives)
        {
            if (prim.vao == 0)
                continue;
            if (prim.usage == selva::world::StaticMeshUsage::Collision)
                continue;
            selva::render::setSceneTint(1.0f);
            selva::render::setSceneBaseColor(
                glm::vec3(prim.base_color[0], prim.base_color[1], prim.base_color[2]));
            glBindVertexArray(prim.vao);
            glDrawElements(GL_TRIANGLES, prim.index_count, GL_UNSIGNED_INT, nullptr);
        }
    }

    // Restore identity so subsequent draws aren't offset by our last
    // pickup's transform.
    selva::render::setSceneModel(glm::mat4(1.0f));
}

} // namespace selva::render
