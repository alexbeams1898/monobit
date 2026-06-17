#pragma once

// Per-bone deformation post-pass on the bone palette. Runs AFTER
// PoseSampler::update() (which fills bone_palette from the current
// animation pose) and BEFORE the renderer uploads the palette to the
// shader. The deformation mutates bone_palette in-place: vertices
// skinned to deformed joints render at their new positions, hurtbox
// queries through jointWorldPos see the un-deformed positions
// (intentional -- hit detection works against the animation pose,
// not the visual deformation).
//
// One function for every per-bone appearance parameter. Today:
// head_scale. Tomorrow: shoulder_width, arm_length, leg_length, etc.
// Each new axis adds one stanza inside applyAppearanceDeformation.
//
// Identity case (every parameter at its default): early return; no
// palette mutation, no cost.

namespace selva::anim
{
struct PoseSampler;
struct SkeletonJointMap;
} // namespace selva::anim

namespace selva::gameplay
{

struct Appearance;

// Apply per-bone deformations driven by the Appearance struct to
// sampler.bone_palette. Mutates the palette in-place. Safe to call
// with default-constructed Appearance (no-op).
//
// `jmap` is the actor's skeleton joint map; empty `head` field
// makes head_scale a no-op for that skeleton (wolf, etc.).
void applyAppearanceDeformation(selva::anim::PoseSampler& sampler,
                                const selva::anim::SkeletonJointMap& jmap,
                                const Appearance& appearance);

} // namespace selva::gameplay
