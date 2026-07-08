#include "render/EquippedWeapon.h"

#include "AppState.h"
#include "AppStateGlobal.h"
#include "ecs/Items.h"
#include "gameplay/Actor.h"
#include "items/ItemRegistry.h"
#include "ops/InventoryOps.h"
#include "render/RegionShaders.h"
#include "world/Lights.h"
#include "world/StaticMeshAssets.h"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
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

// Force-load `def`'s visual_weapon into the cache. No-op on cache hit
// or previously-failed path. Returns the cached mesh pointer, or
// nullptr on empty path / load failure. Shared by resolveEquippedMesh
// (which walks profile/inventory to find the ItemDef) and the boot-
// time preloader (which walks every def in itemRegistry() and pre-
// warms the cache so no gameplay frame pays a cold mesh load).
const selva::world::StaticMesh* loadWeaponMeshInto(const engine::ecs::ItemDef& def,
                                                   const std::string& config_path)
{
    if (def.visual_weapon.empty())
        return nullptr;
    if (auto it = meshCache().find(config_path); it != meshCache().end())
        return &it->second;
    if (loadFailureMemo()[config_path])
        return nullptr;

    selva::world::StaticMesh mesh;
    const bool ok = selva::world::loadStaticMesh(def.visual_weapon.c_str(), glm::vec3(0.0f), mesh);
    if (!ok)
    {
        std::fprintf(stderr,
                     "[weapon-render] failed to load mesh '%s' for item '%s'; "
                     "will not retry this session\n",
                     def.visual_weapon.c_str(), config_path.c_str());
        std::fflush(stderr);
        loadFailureMemo()[config_path] = true;
        return nullptr;
    }
    std::fprintf(stderr, "[weapon-render] loaded '%s' for '%s' (%zu primitives)\n",
                 def.visual_weapon.c_str(), config_path.c_str(), mesh.primitives.size());
    std::fflush(stderr);

    auto [inserted_it, _] = meshCache().emplace(config_path, std::move(mesh));
    return &inserted_it->second;
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
    if (def == nullptr)
        return nullptr;

    return loadWeaponMeshInto(*def, inst->config_path);
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
    //
    // The joint matrix's rotation columns (i = 0,1,2) carry the
    // actor's body_scale -- by design, so the visible giant body's
    // bones land where the renderer drew them. But the WEAPON is an
    // independent object with its OWN authored physical size; we do
    // NOT want body_scale to inherit into the weapon mesh. Per the
    // size design pass: a giant holding a Normal sword shows a
    // small-looking sword in their hand (the "find a bigger weapon"
    // mechanic). So we strip body_scale out of the rotation columns,
    // keeping only the translation (= world position of the giant
    // hand) and the unit-rotation orientation. The weapon's grip +
    // weaponSizeVisualScale then apply on top, fully decoupled from
    // body_scale.
    glm::mat4 joint_world = actor.sampler.jointWorldMatrixWithActor(joint_idx);
    const float body_scale =
        (std::abs(actor.appearance.body_scale) > 1e-5f) ? actor.appearance.body_scale : 1.0f;
    if (std::abs(body_scale - 1.0f) > 1e-5f)
    {
        const float inv = 1.0f / body_scale;
        joint_world[0] *= inv;
        joint_world[1] *= inv;
        joint_world[2] *= inv;
    }

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

    // The grip matrix carries the per-weapon authored
    // offset+rotation+grip_scale. Weapon SIZE (Small/Normal/Large)
    // stacks onto the grip's vertex-side scale so the held weapon
    // mesh visibly changes size with the instance's size enum,
    // anchored at the same hand joint. Composition: vertices get
    // weaponSizeVisualScale FIRST (scale around mesh origin), then
    // grip_scale, then the grip's R+T, then the joint world matrix
    // (which already carries body_scale via PoseSampler so the
    // anchor follows a giant hand).
    glm::mat4 grip = buildGripMatrix(*def);
    grip = glm::scale(grip, glm::vec3(engine::ecs::weaponSizeVisualScale(inst->size)));
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

void tickHeldTorchLight(const selva::gameplay::Actor& actor)
{
    // Torch light lifecycle. Runs unconditionally each frame from
    // PerFrameTick — NOT from the render pass, because the render
    // pass returns early when the actor holds nothing (mesh == null),
    // which used to skip the unregister branch and leak the torch
    // light after a swap to unarmed. Called after animation sample
    // so the joint palette is current.
    selva::PlayerProfile* profile = selva::activePlayerProfile();
    if (profile == nullptr)
    {
        engine::world::unregisterLight("player_torch");
        return;
    }
    const engine::ecs::ItemInstance* inst =
        engine::ops::inventory::findById(profile->inventory, profile->equipment.right_hand);
    if (inst == nullptr)
    {
        engine::world::unregisterLight("player_torch");
        return;
    }
    const engine::ecs::ItemDef* def = selva::items::itemRegistry().find(inst->config_path);
    if (def == nullptr || def->config_path != "config/items/weapons/torch.json")
    {
        engine::world::unregisterLight("player_torch");
        return;
    }
    const int joint_idx = actor.sampler.findJoint(kRightHandJoint);
    if (joint_idx < 0)
    {
        engine::world::unregisterLight("player_torch");
        return;
    }
    glm::mat4 joint_world = actor.sampler.jointWorldMatrixWithActor(joint_idx);
    const float body_scale =
        (std::abs(actor.appearance.body_scale) > 1e-5f) ? actor.appearance.body_scale : 1.0f;
    if (std::abs(body_scale - 1.0f) > 1e-5f)
    {
        const float inv = 1.0f / body_scale;
        joint_world[0] *= inv;
        joint_world[1] *= inv;
        joint_world[2] *= inv;
    }
    glm::mat4 grip = buildGripMatrix(*def);
    grip = glm::scale(grip, glm::vec3(engine::ecs::weaponSizeVisualScale(inst->size)));
    const glm::mat4 final_model = joint_world * grip;

    const glm::vec4 tip_local{0.0f, 0.25f, 0.0f, 1.0f};
    const glm::vec3 tip_world{final_model * tip_local};

    engine::world::LightSource ls;
    ls.position = tip_world;
    ls.color = glm::vec3(1.00f, 0.55f, 0.22f);
    // Intensity tuned against the inverse-square + windowing
    // attenuation model. 2.5 gives ~2.5 at the flame, ~0.6 at 3m
    // out, ~0.15 at 6m, ~0.03 at 12m — matches the visible falloff
    // of a real hand-held torch.
    ls.intensity = 2.5f;
    ls.radius = 12.0f;
    ls.flicker_amp = 0.15f;
    ls.flicker_freq = 3.0f;
    // Small flame sprite — a real torch flame is roughly the size
    // of a fist, not a bonfire.
    ls.sprite_size_override = 0.15f;
    ls.debug_name = "player_torch";
    engine::world::registerOrUpdateLight("player_torch", ls);
}

void clearEquippedWeaponCache()
{
    for (auto& [_, mesh] : meshCache())
        selva::world::freeStaticMeshGLResources(mesh);
    meshCache().clear();
    loadFailureMemo().clear();
}

void preloadAllEquippedWeaponMeshes()
{
    const auto& items = selva::items::itemRegistry();
    int loaded = 0;
    for (const auto& [path, def] : items.defs)
    {
        if (def.visual_weapon.empty())
            continue;
        if (loadWeaponMeshInto(def, path) != nullptr)
            ++loaded;
    }
    std::fprintf(stderr, "[weapon-render] pre-warmed %d weapon mesh(es)\n", loaded);
    std::fflush(stderr);
}

} // namespace selva::render
