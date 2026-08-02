#pragma once

#include <string>
#include <unordered_map>

// Terrain surfaces: the physics of each ground-tile type. A ground tile is tagged
// with a surface in the LDtk tileset editor; the importer resolves that tag to a
// surface name and looks its properties up here. This is the single source of terrain
// walkability -- there is NO hand-painted collision layer. Water blocks because the
// water surface is not walkable, not because someone painted a solid cell over it, so
// collision can never drift from the art. The surface name is also the join key for
// per-surface footstep audio (config/footsteps.json). See config/surfaces.json.
namespace surfaces
{

struct Surface
{
    bool walkable = true;
};

// The loaded surface table + the default surface for untagged tiles (the bare grass
// fill). Lookups fall back to the default, so an unknown/untagged tile is never a
// hole in the map.
struct Config
{
    std::unordered_map<std::string, Surface> surfaces;
    // Matches the LDtk Surface enum namespace (capitalized) -- the tag the importer writes.
    // A lowercase default would never match a real tag, silently breaking the fallback.
    std::string default_surface = "Grass";

    // Walkability of a named surface: the surface's flag, or the default surface's,
    // or true (never trap the player on an unknown tile). Empty name -> default.
    bool walkable(const std::string& name) const;
};

// Load the surface table from config/surfaces.json (silent no-op -> defaults if the
// file is missing/unparseable, keeping the importer permissive).
void load(Config& cfg, const std::string& path);

} // namespace surfaces
