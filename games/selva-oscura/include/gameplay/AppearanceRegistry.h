#pragma once

#include <string>
#include <vector>

namespace selva::gameplay
{

// One entry in the appearance-slider registry. `id` is the stable key
// stored in Appearance.values + appearance.json; `label` is the
// player-facing string in the creator UI; `min/max/default` define
// slider range + the value an unconfigured character gets.
//
// `applies_to` declares how the deformation pass consumes this
// slider's value:
//
//   ---- Scale kinds (write to bone scales / model matrix) ----
//   - "bone_scale"           : `param` is one ozz joint name; subtree
//                              scaled by value (1.0 = no change).
//   - "bone_scale_pair"      : `param` is "boneL,boneR"; both subtrees
//                              scaled by value. L/R symmetric body
//                              parts (arms, legs).
//   - "uniform_scale"        : `param` ignored; value multiplies the
//                              actor's model matrix (renderer-side).
//
//   ---- Morph kinds (write to glTF shape-key weights) ----
//   - "morph_target"         : Unipolar. `param` is the glTF morph
//                              target name; weight = slider value
//                              (typically 0..1). Used for one-way
//                              add-only shapes (e.g. nose_hump).
//   - "morph_pair"           : Bipolar (-1..+1). `param_decr` +
//                              `param_incr` name the matched -decr /
//                              -incr MPFB2 shape pair. Negative slider
//                              writes |value| to param_decr; positive
//                              writes value to param_incr. Used for
//                              MPFB2's symmetric pair patterns
//                              (nose-width-decr / nose-width-incr).
//   - "morph_pair_mirrored"  : Bipolar + L/R symmetric. `param_decr`
//                              and `param_incr` are comma-separated
//                              pairs "left,right" so a single slider
//                              drives BOTH sides. Standard for face
//                              features (ear_size, eye_size, etc.).
//
//   ---- Macro kinds (frozen at character bake) ----
//   - "macro"                : `param` is the MPFB2 macro property
//                              name (age / weight / muscle / caucasian
//                              / etc.). NOT runtime-tweakable; baked
//                              into the per-character .glb at creator
//                              commit. Re-bake required to change.
//
// `category` is the UI grouping for the Souls-style section navigator
// (identity / face_structure / eyes / nose / mouth / ears / proportions
// / skin / hair / eye_color). Default "misc" if unset. Drives which
// section the slider appears in.
//
// `player_visible=false` hides the slider from the creator UI but the
// runtime + save/load still honor the value. Used for dev-only knobs
// (body_scale) and archetype-authored values (foundling body_scale).
struct AppearanceSliderDef
{
    std::string id;
    std::string label;
    float min = 0.0f;
    float max = 1.0f;
    float default_value = 1.0f;
    std::string applies_to;

    // Single-target kinds (bone_scale, bone_scale_pair, uniform_scale,
    // morph_target, macro) use `param`. Bipolar pair kinds (morph_pair,
    // morph_pair_mirrored) use `param_decr` + `param_incr` instead
    // and leave `param` empty.
    std::string param;
    std::string param_decr;
    std::string param_incr;

    std::string category;
    bool player_visible = true;
};

// Process-wide registry. Loaded once at startup from
// config/appearances/sliders.json; queried at runtime by the
// CharacterCreationScreen (to build sliders), AppearanceDeformation
// (to apply values), and Appearance load/save (to enumerate fields).
class AppearanceRegistry
{
  public:
    // Load from JSON. Returns count loaded (0 + logs on failure;
    // existing entries cleared first).
    int loadFromFile(const std::string& path);

    // All defs (caller-iterable for UI + serialization).
    const std::vector<AppearanceSliderDef>& all() const
    {
        return defs_;
    }

    // Lookup by id; returns nullptr if missing.
    const AppearanceSliderDef* find(const std::string& id) const;

    // Singleton accessor; the registry is process-wide.
    static AppearanceRegistry& instance();

  private:
    std::vector<AppearanceSliderDef> defs_;
};

} // namespace selva::gameplay
