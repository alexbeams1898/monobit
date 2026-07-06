#pragma once

// Authored character record -- the on-disk shape produced by the
// Effigie designer and consumed by the game at load time. Wraps the
// existing Appearance struct plus the identity slice (name, class,
// stats, hand-held items) that isn't part of pure appearance.
//
// File-schema notes:
//   * Appearance keys sit at the TOP LEVEL (body_type, morphs,
//     hair_style_id, ...). Pre-existing files that carried only
//     Appearance keys (larva_fresh.json, keeper.json, humanoid.json)
//     load into an AuthoredCharacter with default identity fields.
//     No file rewrite needed.
//   * Identity keys (display_name, player_class, stats, rh_item,
//     lh_item) are optional. Missing = defaults.
//   * Round-trip is stable: saveAuthoredCharacter followed by
//     loadAuthoredCharacter returns byte-identical fields (subject to
//     float precision).

#include "AppState.h"       // PlayerClass
#include "gameplay/Actor.h" // Stats
#include "gameplay/Appearance.h"

#include <string>

namespace selva::gameplay
{

struct AuthoredCharacter
{
    // Lang-map key for the on-screen name. Empty when the file
    // authors an appearance with no character attached (larva files,
    // reference humanoids). NEVER stores the literal string --
    // every player-visible name flows through selva::lang::resolve
    // so insight tiers (???  -> Guide) can swap the displayed text.
    // Example: "interact.npc.guide.display_name".
    std::string display_name_key;

    Appearance appearance;

    // Combat identity. `player_class = None` means the character has
    // no class -- correct for the Guide (unsigned) and for enemies
    // authored via this pipeline. Stats default to the 1,1,1,1,1,1,1
    // starting quad so a fresh file has sane values.
    Stats stats;
    PlayerClass player_class = PlayerClass::None;

    // Hand equipment. Item registry ids ("shortsword", "lantern",
    // ...); empty string = unarmed / empty hand. No armor / bag /
    // consumables slots authored here yet -- those follow when the
    // first authored character needs them.
    std::string rh_item;
    std::string lh_item;

    // Presence flags -- true iff the loaded JSON explicitly contained
    // the corresponding key. Spawn sites overlay ONLY what's present:
    // a larva character file with only appearance keys leaves the
    // spawned Actor's stats/class/equipment at archetype defaults;
    // the Guide's file with stats + player_class + rh_item overlays
    // those onto the spawned Actor. Empty appearance keys (default
    // body_scale = 1.0) are NOT tracked as "not present" -- the
    // Appearance struct is always applied wholesale; the granular
    // opt-in is for the identity slice only. Default-constructed
    // AuthoredCharacter has all flags false (nothing to apply).
    bool has_display_name_key = false;
    bool has_stats = false;
    bool has_player_class = false;
    bool has_rh_item = false;
    bool has_lh_item = false;
};

// Read an AuthoredCharacter from JSON. Missing file / parse error
// returns a default-constructed record with defaults for every field
// (the loader also logs the failure). Pre-existing Appearance-only
// files load cleanly -- identity fields take defaults.
AuthoredCharacter loadAuthoredCharacter(const std::string& path);

// Write an AuthoredCharacter back to JSON at the given path. Returns
// true on success, false on any I/O or serialization error (the
// caller decides whether to surface the failure). Emits the same
// top-level Appearance keys the game already reads via
// loadAppearance, so an AuthoredCharacter file can be consumed by the
// old code path -- the identity keys are simply ignored.
bool saveAuthoredCharacter(const std::string& path, const AuthoredCharacter& character);

} // namespace selva::gameplay
