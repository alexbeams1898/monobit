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
    std::string ambient_track = "assets/audio/ambient_meadow.ogg";
    float ambient_volume = 0.55f;
    int ambient_fade_in_ms = 3000;
};

// Load from config/world.json (silent no-op -> defaults if missing).
void load(Config& cfg, const std::string& path);

} // namespace world_config
