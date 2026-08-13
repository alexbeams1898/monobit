#pragma once

#include "systems/DescentSystem.h"

#include <map>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// SaveGame -- the job that persists. ONE exterminator, one house, one
// continuous accumulation: there is no run to be an instance of, so there are
// no slots and no save verb. The game keeps where he got to and he goes back
// to it.
//
// What is here is only what cannot be rebuilt. Authored config is loaded from
// JSON every launch and never saved; every entity, wave, particle and position
// is rebuilt on arrival. A floor keeps its space and its spent holes -- the
// layout comes back from the seed and the unfinished holes muster again, which
// is what makes resuming mid-dig the same act as walking in.
//
// The generic file half -- where a save lives, reading and writing the
// document -- is the engine's (engine::save). This module owns only the SHAPE
// and its migration.
//
// Versioning contract: read, THEN migrate, then use. Reads are tolerant by
// convention, so an absent key takes its default and additive change is free.
// ---------------------------------------------------------------------------

namespace savegame
{

// Bump when the shape changes in a way defaults alone cannot satisfy, and
// teach migrate() to carry the old shape forward.
inline constexpr int kSchemaVersion = 2;

// One thing in the satchel (the item's blueprint is authored).
struct Item
{
    std::string id;
    int quality = 0;
    int count = 1;
};

// What he is: the sheet he spends points into, and what the work has paid so
// far. Level is derived from points spent, so it is not kept.
struct Man
{
    int chemical = 1;
    int physical = 1;
    int biological = 1;
    int endurance = 1;
    int inspection = 1;
    int banked = 0; // earnings not yet spent at the staging area
    int thermos_fill = 0;
    int thermos_sips = 0;
    int tool = 0;         // which of the kit is in his hands
    int health = -1;      // what is left of him; -1 = a save from before it was kept
    float charge = -1.0f; // what is left in the tank; -1 = a save from before it was kept
    std::vector<Item> satchel;
};

// Where he stopped, exactly. A node in the tree, or -1 with an area name for a
// room that is not a floor. The spot is worth keeping because it is still a
// real spot on return: an authored room is the same room, and a floor rebuilds
// its identical layout from its seed. What does NOT come back is the fight --
// the floor musters its unfinished holes again around wherever he is standing.
struct Where
{
    int node = -1;
    std::string area;
    float x = 0.0f;
    float y = 0.0f;
    bool stood = false; // false = a save written before he was anywhere
};

// One exterminator's working life.
struct Data
{
    std::string id; // stable, never shown, never reused
    std::map<std::string, int> record;
    std::vector<descent::Floor> descent;
    Man man;
    Where where;
};

// The file. A LIST of lives, holding exactly one today -- the shell offers a
// single "start" that resumes or begins, and nothing in it can destroy a run.
// The container is a list anyway because a picker is a screen and nothing
// more: choosing to allow two lives later must not mean reshaping the save.
struct File
{
    int schema_version = kSchemaVersion;
    std::vector<Data> lives;
    int minted = 0; // identities ever handed out; only ever counts up
};

// Where a save lives: %APPDATA%/<org>/<app>/. The studio is the org so its
// games group under one folder; the game is the app.
inline constexpr const char* kOrgName = "monobit";
inline constexpr const char* kAppName = "PointOfEntry";

// Read the file, migrated forward. An empty list comes back for no save or an
// unreadable one -- both mean nobody has started. Empty `path` = the
// conventional location.
File load(const std::string& path = {});

// Write the file. False (and a log line) if it could not be written.
bool save(const File& file, const std::string& path = {});

// Is there a life to go back to?
bool exists(const std::string& path = {});

} // namespace savegame
