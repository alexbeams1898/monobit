#pragma once

#include <string>
#include <unordered_map>
#include <vector>

// Hair style registry. Loaded once at boot from config/hair_styles.json.
// Each entry maps a stable id (used in selva::gameplay::Appearance.hair_style_id)
// to a display name (character-creator UI) + mesh path (HairRenderer cache key).
//
// One code path per new hair style: drop the baked .glb into
// assets/characters/hair/<license>/<style>/humanoid_male_hair.glb,
// add the JSON entry, done. No C++ changes.

namespace selva::hair
{

struct HairStyle
{
    // Stable id used by Appearance.hair_style_id. Format:
    // "<license>/<style_folder_name>" (e.g. "cc0/short01",
    // "ccby/elvs_lara_hair"). License prefix lets us split shipping
    // builds by license or attribute variants.
    std::string id;

    // Player-facing name shown in the character creator's Hair tab.
    // Contributor prefixes stripped, underscores replaced with spaces,
    // title-cased at authoring time (see hair_styles.json _comment).
    std::string display_name;

    // Repo-relative path to the male-bake .glb. Loaded on first draw
    // by HairRenderer, cached process-wide, pre-warmed at boot via
    // preloadAllHairMeshes.
    std::string mesh_path_male;

    // License bucket -- "cc0" or "ccby". Used by future filter passes
    // (build-with-only-CC0 flag, etc.). Not consumed by the runtime
    // today; kept for provenance.
    std::string license;

    // Length bucket ("short" | "long"). Drives the character-creator's
    // grouped picker: styles group under length headers so the player
    // sees "Short 1, Short 2 ..." followed by "Long 1, Long 2 ..."
    // rather than one flat list of contributor-named entries.
    std::string length;
};

// Read-only registry. Style() returns nullptr for unknown ids.
struct HairRegistry
{
    // Insertion-ordered list -- matches the JSON authoring order so
    // the character-creator UI shows entries in a predictable pass.
    std::vector<HairStyle> styles;

    // Fast id -> index into `styles`. Rebuilt whenever styles is
    // populated (once, at boot).
    std::unordered_map<std::string, std::size_t> by_id;
};

// Process-wide registry. Empty until loadHairRegistry() runs.
const HairRegistry& hairRegistry();

// Load config/hair_styles.json into the process-wide registry. Called
// once at boot from main(). Files whose JSON parse fails are logged
// and skipped -- never throws.
void loadHairRegistry(const std::string& config_path);

// Lookup by id. Returns nullptr for empty id or unknown ids.
const HairStyle* findHairStyle(const std::string& id);

} // namespace selva::hair
