#pragma once

#include <glm/vec3.hpp>

#include <string>

// Per-actor visual appearance. One shared humanoid rig today; this
// struct holds the parameters that deform it per being. v1: uniform
// body_scale only. Future: limb proportions, head size, skin tone,
// face blendshape weights, contrapasso deformation axes.
//
// Universal across cosmologies -- damned shades, the Unburdened
// Vagrant, the Guide (also Unburdened), the keepers, future divine
// emissaries all read their body shape through this struct. The
// burdened/unburdened distinction is cosmological state; the
// appearance is just "what does this body look like."
//
// One Appearance per Actor. Player's lives on
// PlayerProfile.appearance_path (per-character, persisted across
// saves); enemy archetypes carry a per-archetype appearance_path so
// every instance shares the archetype's authored body shape. Empty
// path = default Appearance (body_scale = 1.0).
//
// Renderers (buildActorModelMatrix) and animation
// (applyActorClipHipDelta) read body_scale through the Actor. Adding
// a new appearance parameter follows the same shape: extend this
// struct, extend the loader, extend the renderer that consumes it.
// Every renderer takes the Actor by ref, so no callsite refactor is
// needed when the struct grows.

namespace selva::gameplay
{

struct Appearance
{
    // Uniform model-space scale applied to the entire body. 1.0 = the
    // bind-pose authored size. 0.85 = a smaller body; 1.15 = larger.
    // Applies to any skeleton (humanoid OR wolf-quadruped) as a
    // single multiplier on the model matrix -- works at any rig.
    // Per-skeleton deformation parameters (head size, limb
    // proportions, blendshape weights) will layer on top in future
    // milestones and ARE rig-specific.
    float body_scale = 1.0f;

    // Per-channel color tint applied to the skinned mesh shader's
    // tint uniform. {1, 1, 1} = no tint (the mesh's authored colors).
    // Souls-convention range: [0, 1] per channel (loader clamps);
    // values are RGB multipliers, not additive. Used for body
    // coloration (a pale fresh larva at {0.95, 0.92, 0.88}, a
    // sangue-darkened aged larva at {0.55, 0.15, 0.12}, etc).
    // resolveAppearance lerps this between archetype transformations
    // (fresh -> aged larva burn) alongside body_scale and every
    // future appearance axis.
    glm::vec3 color = glm::vec3(1.0f);

    // Per-bone scale on the head joint. 1.0 = no change (default,
    // every legacy archetype reads this and renders identical to
    // pre-head_scale behavior). 1.3 = larger head; 0.8 = smaller.
    // Applied as a post-pass on the bone palette AFTER PoseSampler
    // updates (selva::gameplay::applyAppearanceDeformation). The
    // head joint's world-space matrix gets a uniform scale around
    // its own origin -- the skull grows upward from the neck, the
    // neck itself stays the body's size.
    float head_scale = 1.0f;

    // Per-bone scale on the upper-arm joints (applied to both left
    // and right symmetrically, then recursively to their descendants
    // -- lower arm + hand). 1.0 = no change. 1.3 = longer + thicker
    // "noodle arms"; 0.7 = stubby arms. Independent of body_scale
    // per Souls-style character creator convention: arm_scale is the
    // FINAL visible arm size relative to bind pose, regardless of
    // body's overall size. The deformation pass counter-scales by
    // body_scale internally.
    float arm_scale = 1.0f;

    // Same shape as arm_scale, applied to upper-leg joints + all
    // descendants (knee, ankle, foot). 1.3 = stilt-like long legs;
    // 0.8 = short stocky legs.
    float leg_scale = 1.0f;

    // Per-bone scale on the torso root (Mixamo Spine), applied
    // recursively to its descendants -- which on the humanoid rig
    // INCLUDES the neck/head and the shoulder/arm chains. That's
    // correct anatomy: a broader torso naturally widens shoulders +
    // raises the head. If you want torso-only (head + arms
    // independent), set head_scale + arm_scale to compensate (same
    // counter-scale doctrine head_scale uses against body).
    float torso_scale = 1.0f;
};

// Load an Appearance from JSON. Returns the default-constructed
// struct (body_scale = 1.0) when the path is empty, the file is
// missing, or the file is malformed. Empty path is a normal "no
// appearance specified" signal; missing/malformed log to stderr.
//
// Schema (v1):
//   { "body_scale": 1.0 }
//
// Path is relative to the working directory at boot, matching the
// rest of selva's config loaders.
Appearance loadAppearance(const std::string& path);

// Write an Appearance back to JSON at the given path. Returns true
// on success; logs + returns false on I/O failure. Used by the
// character designer panel's "Save to JSON" button so live slider
// edits can be persisted. Overwrites whatever was there.
bool saveAppearance(const std::string& path, const Appearance& appearance);

} // namespace selva::gameplay

// Post-pass deformation API. Lives in a separate file so the pure
// data struct above doesn't drag PoseSampler in. See
// AppearanceDeformation.h.
