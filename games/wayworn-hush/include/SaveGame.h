#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct GameState;

// ---------------------------------------------------------------------------
// SaveGame -- the pilgrimage that persists. One continuous save (no slots, no
// manual save verb): the game keeps where you got to and you return to it.
//
// This is the PROGRESSION third of the runtime state (see docs/design/SHELL.md):
// what this playthrough has come to know, carry, make, and be, plus where the
// pilgrim stands. Authored config (loaded from JSON every run) is never saved;
// ephemeral frame state (entities, cursors, timers) is rebuilt on load.
//
// The generic file half -- where the save lives, reading/writing the document --
// is the engine's (engine::save). This module owns only the SHAPE and its
// migration.
// ---------------------------------------------------------------------------

namespace savegame
{

// Bump when the shape changes in a way older saves can't satisfy by defaults
// alone; teach migrate() how to carry the old shape forward. Additive fields
// need no bump -- an absent key reads as its default.
inline constexpr int kSchemaVersion = 1;

// What the pilgrim has come to know: the observation record. Mirrors the
// "record" half of observations::State (the authored observables/thoughts are
// reloaded from config, not saved).
struct Record
{
    std::unordered_map<std::string, int> observed_tier; // spot -> deepest tier reached
    std::unordered_set<std::string> fired;              // thought ids that landed
    std::unordered_set<std::string> flags;              // world/event flags set
    std::unordered_set<std::string> taken;              // one-shot deed ids performed (spot:action)
};

// What the pilgrim has become: the progression half of growth::GrowthState (the
// authored stat NAMES + colors come from config).
struct Self
{
    int spirit_exp = 0;
    std::unordered_map<std::string, int> stat_levels; // both tiers, keyed by stat name
    std::unordered_map<std::string, int> buff_levels; // buff id -> level owned
};

// One carried item (mirrors inventory::ItemInstance; the blueprint is authored).
struct Item
{
    std::string id;
    int quantity = 1;
    bool is_new = false;
};

// One written notebook entry (mirrors notebook::Entry).
struct NotebookEntry
{
    int kind = 0; // observations::LineKind as an int (stable across the file)
    std::string text;
    std::string faculty;
    int difficulty = 0;
    int day = 0;
};

// Where the pilgrim stands. Region id is reserved for when the world is more
// than one place; empty means "the only region there is". `walked` distinguishes
// "never set out" from "set out and happens to be at the origin" -- without it a
// fresh pilgrim resumes at world (0,0) instead of the map's spawn.
struct Place
{
    float x = 0.0f;
    float y = 0.0f;
    std::string region;
    bool walked = false;
};

// One pilgrim's walk: everything this playthrough has become. `id` is what the
// pilgrim IS -- stable, never shown, never reused; `name` is only what's shown.
// Keying by identity rather than by name is what lets two pilgrims share a name
// without colliding, makes renaming free, and keeps a delete from taking a
// namesake with it. See docs/design/SHELL.md.
struct Data
{
    std::string id;
    std::string name;
    Record record;
    Self self;
    std::vector<Item> satchel;
    std::vector<NotebookEntry> notebook;
    std::unordered_set<std::string> known_recipes; // realized/taught recipe ids
    std::unordered_set<std::string> announced;     // unlock ids already toasted
    double clock_seconds = 0.0;                    // in-world time elapsed
    Place place;
};

// The file: every pilgrim, plus whatever is true of the installation rather than
// of any one of them.
struct File
{
    int schema_version = kSchemaVersion;
    std::vector<Data> pilgrims;
    // How many identities have ever been minted -- NOT how many exist. The roster
    // can't answer that: forgetting a pilgrim erases the evidence they were here,
    // and a reused id would let a new pilgrim inherit a dead one's identity. Only
    // ever counts up.
    int minted = 0;
};

// Where the save lives: %APPDATA%/<kOrgName>/<kAppName>/ (engine::save::dir). The studio
// is the org so its games group under one folder; the game is the app.
inline constexpr const char* kOrgName = "monobit";
inline constexpr const char* kAppName = "WaywornHush";

// Read the file, migrated forward. An empty roster comes back for no save or an
// unreadable one -- both mean "nobody has walked yet". `path` empty = the
// conventional location.
File load(const std::string& path = {});

// Write the file. Returns false (and logs) if it couldn't be written.
bool save(const File& file, const std::string& path = {});

// Carry an older shape forward to kSchemaVersion. Runs AFTER read, never during
// it -- a read is a plain deserialize; this is where shape history lives.
void migrate(File& file);

// --- the roster ---------------------------------------------------------------

// The pilgrim with this id, or null. Pure lookup: it does NOT care what phase the
// app is in -- a caller that shouldn't be saving is the caller's problem, not a
// silent null that turns a save into a no-op.
Data* find(File& file, const std::string& id);
const Data* find(const File& file, const std::string& id);

// Add a pilgrim under `name` (which need not be unique -- identity is the id).
// Returns their new id.
std::string add(File& file, const std::string& name);

// Forget a pilgrim. Removes exactly the one with this id and no namesake.
void remove(File& file, const std::string& id);

// --- the bridge to live state -------------------------------------------------
// Pure over GameState so the shape and the runtime can't drift apart silently:
// one place turns the world into a save, one place turns a save into the world.

// Write the live walk out of GameState (+ where the player stands) into `out`. Identity
// (id/name) is left alone -- the world doesn't own who the pilgrim is.
void capture(const GameState& gs, float player_x, float player_y, Data& out);

// Apply a save onto a GameState whose AUTHORED config is already loaded. Does
// not touch config or spawn anything; the caller places the player at
// `data.place`.
void apply(const Data& data, GameState& gs);

} // namespace savegame
