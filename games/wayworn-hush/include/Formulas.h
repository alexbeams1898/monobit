#pragma once

#include <string>

// Stat-driven formulas: the shared home for "how a stat changes what you experience." A core
// pillar of the game -- your stats change how you see the world (Perception brightens the
// glow that hints at encounters; more mappings join here as the pillar grows). Following the
// studio convention (see prison-escape/selva Formulas): the COEFFICIENTS live in
// config/formulas.json; the MATH lives in C++ (the accessors below). Never hardcode formula
// constants -- edit the JSON. See docs/design/GAME-SYSTEMS.md.
namespace formulas
{

// Coefficients, loaded from config/formulas.json (defaults are the fallback for any missing
// key). Each sub-struct is one mapping; add a struct + a JSON section + an accessor to grow.
struct Config
{
    // Glow brightness SOFT-caps toward `max` as Perception grows -- it approaches but never
    // slams into the ceiling (diminishing returns, no hard clamp corner). `half_at` is the
    // Perception level that reaches HALF of max; below it the hint is faint (search-for-
    // answers), above it it keeps brightening gently. Same for every spot.
    //   brightness = max * Perception / (Perception + half_at)
    struct Glow
    {
        float max = 0.55f;
        float half_at = 6.0f;
    } glow;
};

// Load coefficients from config/formulas.json (silent no-op -> defaults if missing).
void load(Config& cfg, const std::string& path);

// The peak glow alpha for a spot given the player's Perception level. The one place the glow
// math lives; the glimmer reads this. Clamped to [0, glow.max].
float glowBrightness(const Config& cfg, int perception);

} // namespace formulas
