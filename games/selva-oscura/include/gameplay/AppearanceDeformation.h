#pragma once

#include <glm/mat4x4.hpp>

#include <vector>

// Per-bone deformation post-pass on a bone palette. Runs AFTER
// PoseSampler::update() (which fills the source palette from the
// current animation pose) and BEFORE the renderer uploads the palette
// to the shader.
//
// Reads sampler.bone_palette + the Appearance + the SkeletonJointMap,
// writes the deformed palette into `out_palette`. Does NOT mutate
// the sampler's own palette -- that's important because the
// gameplay scene draws the player every frame regardless of whether
// the sampler ticked this frame (e.g. while the pause menu is open
// in front of the still-rendering world). In-place mutation would
// compound the deformation each frame the sampler doesn't refill
// the palette, causing the figure to "explode" visibly.
//
// One stanza per per-bone parameter today: head_scale, arm_scale,
// leg_scale. Each new axis adds one stanza inside
// applyAppearanceDeformation.
//
// Identity case (every parameter at its default): just copies the
// source palette through and returns; no per-bone work.

namespace selva::anim
{
struct PoseSampler;
struct SkeletonJointMap;
} // namespace selva::anim

namespace selva::gameplay
{

struct Appearance;

// Compute the deformed palette into `out_palette` (resized to match
// sampler.bone_palette). `out_palette` is the caller-owned scratch
// buffer for this actor; persisted between frames is fine + cheap
// (vector reuses storage). Pass the same vector to drawSkeletalMesh
// as the bone palette argument.
void applyAppearanceDeformation(selva::anim::PoseSampler& sampler,
                                const selva::anim::SkeletonJointMap& jmap,
                                const Appearance& appearance, std::vector<glm::mat4>& out_palette);

} // namespace selva::gameplay
