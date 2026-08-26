#pragma once

#include "formats/SpriteDefLoader.h"
#include "systems/DescentSystem.h"

#include <string>
#include <unordered_map>
#include <vector>

// WHAT A KIND OF HOLE IS, and what a kind of space may grow.
//
// Two documents meet here and neither belongs to the descent itself. A HOLE FILE
// (config/holes/*.json) says what one looks like, whether it is cut into a wall or the ground,
// and what it opens onto. A ROOM TYPE (config/rooms/*.json) says which of them a space may
// grow and in what mix, plus the rules that bound the growing.
//
// Lifted out of the descent because reading those two documents is not what the descent DOES --
// it digs, it remembers, it takes him through. This is the vocabulary it digs with.
namespace holes
{

// What a marker becomes: its kind's look and where pests surface. Rolled from the floor's seed,
// so a layout is the same holes every time.
struct Kind
{
    std::string path;
    sprite_def::Def def;
    std::string opens; // the kind of space it leads to; empty = the descent's default
    bool on_wall = false;
    int weight = 1;
    // The first species its file names, for whoever needs a face to put to the hole without
    // running its whole mix.
    std::string first_pest;
};

// What a KIND OF SPACE imposes on its holes beyond the mix itself.
struct Rules
{
    int rooms_per_floor = 2;
    int wall_depth = 2;
};

// One kind, read from its file. Missing or malformed comes back with `def.ok` false, which is
// what makes an unnamed hole a loud placeholder rather than a crash.
Kind load(const std::string& path);

// The same, cached -- for the questions asked about a kind by path rather than by hand.
const Kind& facts(const std::string& path);
bool onWall(const std::string& path);

// THE DESCENT'S DEFAULT SPACE, for a hole that names none: the house's own foundation.
const std::string& defaultType();

// Which kind of space a room is, with the fallback applied once here so nothing downstream has
// to remember that an empty string means the default.
const std::string& typeOf(const descent::Room& room);

// This kind of space's mix of hole kinds, plus any letter-pinned kinds. Read through the type's
// base chain, so a type that does not name a mix inherits the one it varies from.
Rules loadTable(const std::string& typePath, std::vector<Kind>& kinds,
                std::unordered_map<char, std::string>& pinned);

// The kind at `path`, appending it to `kinds` if it is not already there. Returns a pointer INTO
// that vector, so a caller must resolve it after every decision that could append.
const Kind* byPath(std::vector<Kind>& kinds, const std::string& path);

} // namespace holes
