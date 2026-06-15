#include "render/EquippedWeapon.h"

#include "AppState.h"
#include "AppStateGlobal.h"
#include "ecs/Items.h"
#include "gameplay/Actor.h"
#include "items/ItemRegistry.h"
#include "ops/InventoryOps.h"
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

// Process-wide cache of loaded weapon meshes keyed by config_path
// (NOT by visual_weapon path -- two items pointing at the same mesh
// would still cache separately, which is fine; the dedup payoff is
// not worth the second-key lookup). First equip of a given weapon
// loads + uploads; subsequent equips hit the cache. Cleared by
// clearEquippedWeaponCache at shutdown.
std::unordered_map<std::string, selva::world::StaticMesh>& meshCache()
{
    static std::unordered_map<std::string, selva::world::StaticMesh> cache;
    return cache;
}

// First-load failure memo so we don't spam the log on a per-frame
// retry of a broken path. A failed lookup gets pinned to "tried and
// gave up"; a fresh boot retries (cache is process-lifetime).
std::unordered_map<std::string, bool>& loadFailureMemo()
{
    static std::unordered_map<std::string, bool> memo;
    return memo;
}

// Resolve the right-hand-equipped weapon's mesh, loading on first
// touch. Returns nullptr if no profile / nothing equipped / item
// has no visual_weapon / load failed.
const selva::world::StaticMesh* resolveEquippedMesh()
{
    selva::PlayerProfile* profile = selva::activePlayerProfile();
    if (profile == nullptr)
        return nullptr;

    const engine::ecs::ItemInstanceId equipped_id = profile->equipment.right_hand;
    if (equipped_id == engine::ecs::kInvalidItemInstanceId)
        return nullptr;

    const engine::ecs::ItemInstance* inst =
        engine::ops::inventory::findById(profile->inventory, equipped_id);
    if (inst == nullptr)
        return nullptr;

    const auto& items = selva::items::itemRegistry();
    const engine::ecs::ItemDef* def = items.find(inst->config_path);
    if (def == nullptr || def->visual_weapon.empty())
        return nullptr;

    // Cache hit.
    if (auto it = meshCache().find(inst->config_path); it != meshCache().end())
        return &it->second;

    // Skip retries on a previously-failed path.
    if (loadFailureMemo()[inst->config_path])
        return nullptr;

    selva::world::StaticMesh mesh;
    const bool ok = selva::world::loadStaticMesh(def->visual_weapon.c_str(), glm::vec3(0.0f), mesh);
    if (!ok)
    {
        std::fprintf(stderr,
                     "[weapon-render] failed to load mesh '%s' for item '%s'; "
                     "will not retry this session\n",
                     def->visual_weapon.c_str(), inst->config_path.c_str());
        std::fflush(stderr);
        loadFailureMemo()[inst->config_path] = true;
        return nullptr;
    }
    std::fprintf(stderr, "[weapon-render] loaded '%s' for '%s' (%zu primitives)\n",
                 def->visual_weapon.c_str(), inst->config_path.c_str(), mesh.primitives.size());
    std::fflush(stderr);

    auto [inserted_it, _] = meshCache().emplace(inst->config_path, std::move(mesh));
    return &inserted_it->second;
}

constexpr const char* kRightHandJoint = "mixamorig:RightHand";

} // namespace

glm::mat4 buildGripMatrix(const engine::ecs::ItemDef& def)
{
    constexpr float kDeg2Rad = 3.14159265358979323846f / 180.0f;
    glm::mat4 grip(1.0f);
    grip = glm::translate(grip, glm::vec3(def.grip_offset_x, def.grip_offset_y, def.grip_offset_z));
    grip = glm::rotate(grip, def.grip_rot_deg_z * kDeg2Rad, glm::vec3(0.0f, 0.0f, 1.0f));
    grip = glm::rotate(grip, def.grip_rot_deg_y * kDeg2Rad, glm::vec3(0.0f, 1.0f, 0.0f));
    grip = glm::rotate(grip, def.grip_rot_deg_x * kDeg2Rad, glm::vec3(1.0f, 0.0f, 0.0f));
    grip = glm::scale(grip, glm::vec3(def.grip_scale));
    return grip;
}

void drawEquippedWeapon(const selva::gameplay::Actor& actor)
{
    const selva::world::StaticMesh* mesh = resolveEquippedMesh();
    if (mesh == nullptr || mesh->primitives.empty())
        return;

    const int joint_idx = actor.sampler.findJoint(kRightHandJoint);
    if (joint_idx < 0)
        return;

    // World matrix of the right-hand joint, with the actor's pos+yaw
    // already baked in by setActorPlacement (called per-frame by
    // PerFrameTick before render).
    const glm::mat4 joint_world = actor.sampler.jointWorldMatrixWithActor(joint_idx);

    // Per-weapon grip transform: slides + rotates + scales the mesh
    // in the hand's local space so the GRIP POINT of the handle lands
    // at the wrist joint. Authored in ItemDef.grip_offset_* /
    // grip_rot_deg_* / grip_scale; tunable in the F1 Tuning panel
    // with write-back to JSON. Composition order: T * R * S applied
    // AFTER the joint world matrix (so translation/rotation are in
    // joint-local space, intuitive for tuning).
    //
    // Look up the ItemDef again here -- we already paid for it in
    // resolveEquippedMesh, but caching it would require threading
    // the def through. Per-frame lookup is fine (registry is a flat
    // unordered_map).
    selva::PlayerProfile* profile = selva::activePlayerProfile();
    const engine::ecs::ItemInstance* inst =
        engine::ops::inventory::findById(profile->inventory, profile->equipment.right_hand);
    const engine::ecs::ItemDef* def = selva::items::itemRegistry().find(inst->config_path);

    const glm::mat4 grip = buildGripMatrix(*def);
    const glm::mat4 final_model = joint_world * grip;
    selva::render::setSceneModel(final_model);

    for (const auto& p : mesh->primitives)
    {
        if (p.vao == 0)
            continue;
        if (p.usage == selva::world::StaticMeshUsage::Collision)
            continue;
        selva::render::setSceneTint(1.0f);
        selva::render::setSceneBaseColor(
            glm::vec3(p.base_color[0], p.base_color[1], p.base_color[2]));
        glBindVertexArray(p.vao);
        glDrawElements(GL_TRIANGLES, p.index_count, GL_UNSIGNED_INT, nullptr);
    }

    // Restore identity model matrix so subsequent draws (any caller
    // assuming default identity) aren't offset by our hand transform.
    selva::render::setSceneModel(glm::mat4(1.0f));
}

void clearEquippedWeaponCache()
{
    for (auto& [_, mesh] : meshCache())
        selva::world::freeStaticMeshGLResources(mesh);
    meshCache().clear();
    loadFailureMemo().clear();
}

} // namespace selva::render
