#pragma once

#include <string>

// The region's asset identity (config/world.json): the LDtk map, its render atlas, the
// tileset-name substring the importer matches, and the ambient audio bed. One source for
// the paths that were otherwise repeated across main.cpp and the importer.
namespace world_config
{

struct Config
{
    std::string ldtk = "assets/tilesets/source/overworld.ldtk";
    std::string tileset_png = "assets/tilesets/overworld.png";
    std::string tileset_name = "Overworld"; // substring matched against .ldtk tileset defs
    // The level a NEW walk begins in (LDtk level identifier). Empty = the project's
    // first level. A resumed walk uses its saved region instead.
    std::string start_level;
    std::string ambient_track = "assets/audio/ambient_meadow.ogg";
    float ambient_volume = 0.55f;
    int ambient_fade_in_ms = 3000;
    // Seconds each half of the warp fade takes (black-out, then black-in after the
    // swap). Zero = no fade, instant region switch.
    float warp_fade_seconds = 0.12f;
    // Door-transition SFX, played as the fade begins: `exit` when leaving an
    // interior, `enter` when stepping in from outside. Empty path = silent.
    std::string warp_enter_sfx = "assets/audio/door_enter.ogg";
    std::string warp_exit_sfx = "assets/audio/door_exit.ogg";
    float warp_sfx_volume = 0.9f;
};

// Load from config/world.json (silent no-op -> defaults if missing).
void load(Config& cfg, const std::string& path);

} // namespace world_config
