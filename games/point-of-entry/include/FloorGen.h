#pragma once

#include "TileMap.h"

#include <string>
#include <vector>

class EntityManager;

// Builds a floor: hand-authored ASCII rooms, assembled procedurally.
//
// A floor is generated ONCE, when the gadget opens the space, and then it stays
// that way -- it is not re-rolled every time the player walks back through. The
// space is unmapped until the robot maps it, and then it is a place (see
// docs/design/PITCH.md, "The gadget"). This header is the generation half; the
// persistence half arrives with the gadget.
//
// The .room format (config/rooms/*.room), one character per tile:
//   '.' or ' '  floor
//   'W'         wall
//   any letter  floor, plus a named marker at that cell -- the game decides what
//               the letter means. 'P' is a point of entry.
namespace floorgen
{

// A marker the generator found in a room template, in WORLD pixels (already
// placed, so this is where the thing actually goes).
struct Marker
{
    char type = 0;
    float x = 0.0f;
    float y = 0.0f;
};

// What generating a floor produced. The tiles themselves are written straight
// into the EntityManager's tile map; this is everything else the caller needs.
struct Floor
{
    float spawn_x = 0.0f; // where the player starts -- centre of the first room
    float spawn_y = 0.0f;
    std::vector<Marker> markers;
    bool ok = false; // false = nothing could be generated (no rooms, bad config)
};

// One room template, parsed. Public so it can be tested without touching disk.
struct Room
{
    int width = 0;
    int height = 0;
    std::vector<int> tiles;      // row-major: tiles[row * width + col]
    std::vector<Marker> markers; // in TILE coordinates at parse time
    std::string name;            // for diagnostics
};

// Parse one ASCII template. `name` is used only in warnings.
Room parseRoom(const std::string& text, const std::string& name = {});

// Generate a floor into `em`'s tile map: load every .room in `roomsDir`, place
// them without overlapping, connect them with corridors, and collect the
// markers. `seed` of 0 picks one from the clock; pass a real seed for a
// repeatable floor (which is what the persistence rule will want).
Floor generate(EntityManager& em, const std::string& configPath, const std::string& roomsDir,
               unsigned seed = 0);

} // namespace floorgen
