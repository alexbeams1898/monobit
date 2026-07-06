#pragma once

#include <glm/mat4x4.hpp>

#include <vector>

// Hair render pass. The actor's Appearance.hair_style_id resolves to
// a standalone rigged .glb (baked by gen_hair.py per style, one .glb
// per hair style). We load that mesh through the same
// meshByArchetypePath primitive that enemy variant meshes use, so
// its inverse-bind matrices resolve against the ACTOR's skeleton --
// the hair's per-vertex bone_indices then reference the actor's
// bone-palette ordering.
//
// The upshot: drawing hair is one call to drawSkeletalMesh with the
// actor's model matrix and bone palette. The hair deforms with the
// actor's head automatically because it's weighted to the head bone,
// and the head bone's world matrix in the palette is the actor's
// live-posed head.
//
// Empty Appearance.hair_style_id = no hair, no draw call. Unknown id
// or failed mesh load = memoed as failed, no retry, no crash.

namespace selva::gameplay
{
struct Actor;
}

namespace selva::render
{

// Per-frame: draw the actor's hair. Runs after the actor's body
// skinned mesh has been drawn AND after PoseSampler::update for the
// frame (so the bone palette is current). No-op when actor has no
// hair_style_id or the resolved mesh isn't loadable.
//
// `bone_palette` MUST be the same palette the caller passed to
// drawSkeletalMesh for the actor's BODY draw this frame. Different
// call sites use different palettes (PerFrameTick uses
// actor.deformed_bone_palette, CharacterPreview uses its own static
// sPreviewPalette), so we take it as an explicit parameter rather
// than reading it off Actor -- otherwise the hair skins against a
// stale/empty palette and the mesh renders as garbage. `view_proj`
// is likewise the same matrix the body draw received.
//
// `body_foot_offset_y` MUST be the same value the caller passed to
// buildActorModelMatrix for the BODY draw. The body's foot_offset_y
// lifts the mesh so its (post-skinning) feet land at pos.y; hair
// must use the SAME lift so it lands in the same world frame as the
// body. Using the hair mesh's own foot_offset_y (its scalp-Y) would
// translate hair down to the boots -- wrong.
void drawHair(const selva::gameplay::Actor& actor, const glm::mat4& view_proj,
              const std::vector<glm::mat4>& bone_palette, float body_foot_offset_y);

// Boot-time pre-warm: walk the hair registry and force-load every
// style's mesh into the archetype-mesh cache so first-touch during
// gameplay is free. Called from main.cpp's preload block, next to
// the other pre-warmers.
void preloadAllHairMeshes();

} // namespace selva::render
