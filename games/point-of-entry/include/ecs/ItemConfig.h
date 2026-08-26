#pragma once

#include <string>
#include <vector>

// Item definitions and instances.
//
// Two ladders, deliberately distinct: RARITY belongs to the item TYPE (how often the world
// offers one at all) and QUALITY to the INSTANCE (how good this particular one came out,
// rolled the moment it drops). A crude rare and a masterwork common are both real things,
// and conflating the ladders is how loot systems go mushy.
//
// Item definitions are FILES under config/items/ -- adding one to the game is authoring data.
// Display names live in the files; these enums are structure, not copy.

enum class Rarity : unsigned char
{
    Common = 0,
    Uncommon,
    Rare,
    Exceptional
};

enum class Quality : unsigned char
{
    Crude = 0,
    Standard,
    Fine,
    Superior
};

// A template, read-only after load. `path` doubles as the item's identity everywhere --
// drop tables and satchels refer to items by file path, one name for one thing.
struct ItemDef
{
    std::string path;
    std::string name;
    Rarity rarity = Rarity::Common;
    int value = 1; // what the town will care about later
    bool ok = false;
};

// One line of a pest's drop table, as its file declares it.
struct DropEntry
{
    std::string item; // ItemDef path
    int min = 1;
    int max = 1;
    float chance = 1.0f;
};

// One dropped/held thing: which item, how it came out, how many.
struct ItemInstance
{
    std::string item; // ItemDef path
    Quality quality = Quality::Standard;
    int count = 1;
};

namespace items
{

// Load every .json under the directory. Loud about files that do not parse.
void load(const std::string& dir);

// Look one up by path. A miss returns a def with ok=false and logs -- a drop table naming a
// missing item is a content bug that must surface, not a silent nothing.
const ItemDef& get(const std::string& path);

} // namespace items
