#pragma once

#include "ecs/Items.h"

#include <glm/mat4x4.hpp>

// Visible weapon render pass. The player's equipped right-hand weapon
// is drawn as a static mesh parented to the right-hand joint each
// frame -- the weapon rides whatever pose the PoseSampler is playing
// (idle, walk, swing). No per-weapon-class swing animation yet; the
// hand bone moves through the unarmed pose and the weapon goes with
// it. Visually correct enough for v1; per-weapon animation is a
// future slice.
//
// Mesh source = engine::ecs::ItemDef.visual_weapon (a .gltf or .glb
// path). Meshes are cached process-wide by config_path; first equip
// of an item loads + uploads to GL, subsequent equips hit the cache.
//
// Per [[ferine-mouth-slot-locked-2026-06-14]] the future left-hand /
// mouth-slot rendering will add sibling functions, but v1 ships right
// hand only.

namespace selva::gameplay
{
struct Actor;
}

namespace selva::render
{

// Per-frame: draw the equipped right-hand weapon for the player.
// Resolves the live profile + equipped item; if nothing is equipped
// (or the item has no visual_weapon), draws nothing. Must be called
// after the actor's skinned mesh has been drawn (and after the
// sampler's setActorPlacement call for the frame) so the joint world
// matrix is current.
void drawEquippedWeapon(const selva::gameplay::Actor& actor);

// Free GL resources for every cached weapon mesh. Called at shutdown
// (or on a hard cache invalidate during dev iteration).
void clearEquippedWeaponCache();

// Pre-warm the weapon-mesh cache: walk every ItemDef in itemRegistry()
// and force-load any def with a non-empty visual_weapon. Same rationale
// as preloadAllPickupMeshes -- first equip of a weapon should not
// stall a gameplay frame with a cold mesh load. Called during the
// loading-screen boot phase, next to the other preload steps.
//
// Trace analysis prior to this fix showed equipped-weapon zone maxing
// at 15.8 ms on first equip (avg 0.01 ms). Post-fix, first-equip cost
// is paid at boot instead.
void preloadAllEquippedWeaponMeshes();

// Build the per-weapon grip transform from ItemDef fields. Pure math
// (no GL, no globals); the live render code multiplies this by the
// hand-bone world matrix. Composition: T(offset) * R_z(rot.z) *
// R_y(rot.y) * R_x(rot.x) * S(scale). Exposed for tests.
glm::mat4 buildGripMatrix(const engine::ecs::ItemDef& def);

} // namespace selva::render
