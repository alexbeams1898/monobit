#pragma once

#include <glm/vec3.hpp>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

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

// One active transformation acting on an actor's body. The world's
// appearance system is a sum of these: an actor's base Appearance
// (what was authored/spawned) plus zero-or-more transforms applied
// at draw time. Generalization of the EnemyArchetype.transform_target_archetype
// field that was used pre-2026-06-29 for fresh->aged larva burns;
// every gameplay event that changes a body (sangue overload, class
// evolution, boss-felled scarring, posture sag, etc.) is now an
// AppearanceTransform.
//
// Authoring rule: each transform's effect is fully captured by the
// snapshot file at `target_snapshot_path`. The snapshot is an
// Appearance JSON containing ONLY the axes this transform touches;
// absent fields mean "this transform doesn't write to that axis."
// `ls config/appearances/transforms/` IS the audit of "every thing
// in the game that can change a body."
//
// Reconciliation: `Appearance.transforms` is REBUILT every frame by
// AppearanceTransformWriter from the registered TransformSources;
// it is pure derivation from world state (flags + stats). Save/load
// restores the world state and the next reconciler tick rebuilds
// the list. Do NOT push transforms into this vector by hand outside
// the reconciler -- they will be overwritten.
//
// Strength composes additively across transforms per axis (the
// resolver lerps base toward each target by each strength and sums
// the deltas). Per-axis clamp lives in the slider registry.
//
// Design: [docs/design/character-canvas.md](docs/design/character-canvas.md)
// *The AppearanceTransform primitive*.
struct AppearanceTransform
{
    // Stable identifier matching the snapshot filename (basename of
    // target_snapshot_path, sans .json). Also the key the reconciler
    // uses to deduplicate -- two TransformSources with the same id
    // collide; that's a bug to catch at registration time.
    std::string id;

    // What kind of world condition drove this transform. Free-form
    // string for now ("flag" | "stat_threshold" | "residence_time" |
    // "evolution_node" | "boss_felled" | "archetype_transform" | ...).
    // Used for diagnostics and for the future debugging UI that
    // shows "why does the player look this way?". Not consumed by
    // the resolver.
    std::string source_kind;

    // config/appearances/transforms/<id>.json -- the snapshot whose
    // axes this transform writes toward.
    std::string target_snapshot_path;

    // Lerp weight from base toward target. 0.0 = transform inactive
    // (resolver skips it as an optimization). 1.0 = full target.
    // Values >1.0 or <0.0 are not clamped at the transform level --
    // per-axis clamping happens at the slider registry.
    float strength = 0.0f;
};

// Body Type discrete enum. Souls-convention "Body Type 1 / 2" toggle
// in the character creator. Maps to one of the parametric humanoid
// skeletons at render time (Type1 -> humanoid_male, Type2 ->
// humanoid_female). Both share the same Mixamo 52-bone topology + the
// same baked clip library; only rest-pose proportions differ.
//
// Discrete (not interpolated) -- archetype-transform lerps that touch
// the rest of Appearance must leave body_type alone (you don't
// gradually become a different skeleton). See resolveAppearance().
enum class BodyType : std::uint8_t
{
    Type1 = 1, // humanoid_male skeleton bundle
    Type2 = 2, // humanoid_female skeleton bundle
};

const char* bodyTypeSkeletonId(BodyType t);

struct Appearance
{
    // Discrete body-type choice. Player-facing as "Body Type 1 / 2"
    // in the character creator; resolves to a skeleton bundle at
    // render time via bodyTypeSkeletonId().
    BodyType body_type = BodyType::Type1;

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

    // Per-bone scale on the torso root joint, applied recursively to
    // its descendants -- which on the humanoid rig INCLUDES the
    // neck/head and the shoulder/arm chains. That's correct anatomy:
    // a broader torso naturally widens shoulders + raises the head.
    // If you want torso-only (head + arms independent), set
    // head_scale + arm_scale to compensate (same counter-scale
    // doctrine head_scale uses against body).
    float torso_scale = 1.0f;

    // Hair style id, keyed against the hair registry loaded from
    // config/hair_styles.json. Empty = bald (no hair rendered).
    // Non-empty = look up the entry, load its mesh_path .glb, draw
    // as a head-parented skinned mesh via HairRenderer::drawHair.
    // Persisted through save/load; per-character on PlayerProfile,
    // authored on enemy archetypes via appearance_path or rolled at
    // spawn when EnemyArchetype.random_hair_style is set.
    std::string hair_style_id;

    // Per-channel RGB tint applied to the hair diffuse. The hair
    // shader runs in Colorize mode (desaturate-then-tint), so this
    // controls the hair's HUE while the diffuse's strand detail
    // provides luminance. Default is a natural medium brown so the
    // creator opens with plausible-looking hair rather than a
    // grayscale luminance-only look. Player-facing as an RGB picker
    // in the character creator's Hair tab; RGB values here are in
    // linear space (the load path linearizes JSON sRGB inputs).
    glm::vec3 hair_tint = glm::vec3(0.25f, 0.15f, 0.08f);

    // Per-channel RGB tint applied to the eye (iris + sclera)
    // primitive in Colorize mode. Same shader path as hair_tint,
    // scoped to the eye submesh via material.role == Eyes (set at
    // load time when the baked material name is "HumanoidEyes").
    // Default is white so the creator opens with the baked iris
    // colour untouched -- the picker is a shift-from-neutral, not a
    // required choice. Values are linear-space; the JSON load path
    // linearizes sRGB inputs.
    glm::vec3 eye_tint = glm::vec3(1.0f);

    // Vertex-morph weights, keyed by glTF morph-target name (which
    // matches the Blender shape-key name = MPFB2 target name = the
    // `param` field in config/appearances/sliders.json entries with
    // applies_to=morph_target). Values typically in [0, 1] but the
    // shader allows any float; negative weights subtract the morph
    // and >1 over-applies it (useful for caricature). Missing key =
    // weight 0 (the rest pose, no morph contribution).
    //
    // Resolved at draw time into a dense vector in the order
    // SkeletalMesh.morph_names declares; per-frame conversion lives
    // in PerFrameTick's drawPlayerSkeletal / drawOrQueueEnemy paths.
    // Saved/loaded via appearance.json's "morphs" sub-object.
    std::unordered_map<std::string, float> morph_weights;

    // Active transformations on this body. Rebuilt every frame by
    // AppearanceTransformWriter from registered TransformSources --
    // do NOT mutate by hand; the next reconciler tick will overwrite.
    // The resolver (resolveLiveAppearance) lerps the base Appearance
    // toward each transform's target by each transform's strength,
    // summing deltas per axis. Empty (default) = no transforms; the
    // base appearance is exactly what the renderer sees.
    std::vector<AppearanceTransform> transforms;
};

// Generic resolver: returns the LIVE Appearance, composing `base`
// with the transforms in `base.transforms`. For each active
// transform (strength > epsilon), loads its target snapshot from
// disk (cached) and lerps each axis from base toward target by
// strength. Multiple transforms compose additively per axis.
//
// This is THE render-path call site -- WorldRenderer reads through
// this. Snapshot files load through a process-wide cache keyed by
// path so repeated calls are cheap. Snapshot fields not present in
// the JSON fall back to base (the snapshot only touches the axes
// it lists).
//
// Engine-side, archetype-agnostic. Used for both player and NPC.
// Per-actor specialization (which transforms are active, with what
// strengths) lives in AppearanceTransformWriter; this function just
// composes whatever it's given.
Appearance resolveLiveAppearance(const Appearance& base);

// Lookup an Appearance field by string id (the slider registry's
// stable key). Returns a pointer to the float field so callers can
// read OR write through it. Returns nullptr for unknown ids. The
// supported ids match the AppearanceSliderDef::id values in
// config/appearances/sliders.json (body_scale, head_scale, arm_scale,
// leg_scale, torso_scale today). Body_type + color stay as discrete
// fields (different types) and are not routed through this lookup.
float* appearanceFieldByName(Appearance& a, const std::string& id);
const float* appearanceFieldByName(const Appearance& a, const std::string& id);

// Lookup a morph weight by name. The map auto-creates the entry at 0.0
// on first access, matching the "unset = rest pose (weight 0)"
// contract. Use this for UI bindings (ImGui::SliderFloat needs a
// stable float* to write into). Read-only lookup returns 0.0 when
// the key isn't present.
float* appearanceMorphWeight(Appearance& a, const std::string& morph_name);
float appearanceMorphWeight(const Appearance& a, const std::string& morph_name);

// Load an Appearance from JSON. Returns the default-constructed
// struct (body_scale = 1.0) when the path is empty, the file is
// missing, or the file is malformed. Empty path is a normal "no
// appearance specified" signal; missing/malformed log to stderr.
//
// Path is relative to the working directory at boot, matching the
// rest of selva's config loaders.
Appearance loadAppearance(const std::string& path);

// Write an Appearance back to JSON at the given path. Returns true
// on success; logs + returns false on I/O failure. Overwrites
// whatever was there.
bool saveAppearance(const std::string& path, const Appearance& appearance);

} // namespace selva::gameplay

// The Appearance-JSON reader/writer helpers below take an
// nlohmann::json object rather than a path, so higher-level record
// types (AuthoredCharacter) can compose the Appearance schema with
// their own top-level keys and still produce ONE authoritative
// on-disk shape. `source_desc` is used only in log messages.
#include <nlohmann/json.hpp>

namespace selva::gameplay
{

void readAppearanceFromJson(const nlohmann::json& j, Appearance& out,
                            const std::string& source_desc);
void writeAppearanceToJson(const Appearance& appearance, nlohmann::json& out);

} // namespace selva::gameplay

// Post-pass deformation API. Lives in a separate file so the pure
// data struct above doesn't drag PoseSampler in. See
// AppearanceDeformation.h.
