#pragma once

// Pickup mesh pass: renders any live selva::loot::Pickup whose
// ItemDef declares a `world_mesh` path as a static .glb mesh in the
// world. Pickups WITHOUT a world_mesh fall back to the glow-sprite
// path (PickupSpritePass) -- mesh pickups and sprite pickups are
// mutually exclusive at draw time.
//
// Routine forage materials (bark, earth, lichen) get meshes that
// blend with the terrain -- discovery is by proximity to the visible
// object, not by spotting a glow. Weapons and special drops keep the
// glow sprite so the player can spot them at distance.
//
// Composition: depth-tested against the scene; uses the scene shader
// program (bound by the caller alongside renderStaticMeshes /
// renderDoors / renderEquippedWeapon). Per-pickup model matrix =
// T(world_pos) * R_y(world_yaw).

namespace selva::render
{

// Free GPU resources for every cached pickup mesh. Called at shutdown.
void clearPickupMeshCache();

// Draw every live loot::Pickup that has a world_mesh path on its
// ItemDef. Scene shader is bound by the caller; this just sets per-
// pickup model matrix + per-primitive base color and issues the
// draws. No GL state changes outside the scene-shader-bound block
// (matches renderEquippedWeapon convention).
void renderPickupMeshes();

} // namespace selva::render
