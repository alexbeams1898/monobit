#pragma once

#include "UnlockCondition.h"

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include <entt/entt.hpp>

class EntityManager;

// The authored characters (config/npcs/*.json): WHO someone is -- art, display
// name, footprint, and how they LIVE (named anims, routines, a schedule). WHERE
// they stand is map authoring (an Npc entity naming an id here), and WHAT they
// say is observation content (a speaker-flagged encounter) -- talking IS
// observing, through the same engine. An animal is just an npc that never
// speaks. See docs/design/GAME-SYSTEMS.md.
namespace npc
{

// A named animation state -- a row of the character sheet, referenced by
// routine steps ("sit", "cleaning"). "walk" and "idle" resolve to the config's
// built-in rows without an entry here.
struct AnimState
{
    int row = 1;
    int frames = 1;
    float duration = 0.0f;
};

// One ambient routine step (first verb key wins, like a scene step):
//   {"wander": px}        -- walk to a random spot within px of the home spot
//   {"move_to": marker}   -- walk to a named map Marker
//   {"face": cardinal}    -- turn
//   {"anim": name}        -- switch to a named anim state
//   {"wait": s | [a, b]}  -- hold (a fixed time, or a random draw per visit)
struct RoutineStep
{
    enum class Kind
    {
        Wander,
        MoveTo,
        Face,
        Anim,
        Wait
    };
    Kind kind = Kind::Wait;
    float radius = 0.0f;   // Wander (world px)
    std::string target;    // MoveTo marker / Face cardinal / Anim name
    float wait_min = 0.0f; // Wait bounds (equal = fixed)
    float wait_max = 0.0f;
};

// One schedule entry: WHEN this routine is the npc's life. First entry whose
// window holds (and whose knowledge gate is met) wins; none = stand idle. Times
// are fractions of the day (authored "HH:MM" on the 24-hour face); from > to
// wraps midnight. `when` is the one gate primitive, so a schedule reacts to
// story flags with no new machinery. Day/season filters arrive with the
// calendar -- inside the clock, not here.
struct ScheduleEntry
{
    double from = 0.0;
    double to = 1.0;
    unlock::Condition when;
    std::string routine;
};

struct Config
{
    std::string id;
    std::string name;    // display name -- shown when their lines surface
    std::string texture; // sprite sheet in the character layout (walk/idle/run rows)
    int frame_width = 64;
    int frame_height = 64;
    int direction_count = 4;
    int max_frames_per_state = 9;
    // The idle row + its frame count (1 = a standing pose per direction).
    int idle_row = 1;
    int idle_frames = 1;
    float idle_duration = 0.0f; // seconds/frame; 0 = static
    // The walk cycle (character-sheet layout defaults), used when a scene walks
    // them somewhere. `walk_speed` is world px/sec.
    int walk_row = 0;
    int walk_frames = 9;
    float walk_duration = 0.09f;
    float walk_speed = 90.0f;
    // Foot collider (world px): a small solid box at the sprite's base, so they
    // Y-sort by where they stand and the player cannot walk through them.
    float collider_w = 22.0f;
    float collider_h = 12.0f;

    // How they live when nothing else has the floor (see AnimState / RoutineStep
    // / ScheduleEntry above). All optional: an npc without a schedule stands
    // where placed, exactly as before.
    std::unordered_map<std::string, AnimState> anims;
    std::unordered_map<std::string, std::vector<RoutineStep>> routines;
    std::vector<ScheduleEntry> schedule;
};

// The schedule entry in force at `day_frac` (0..1 of the day), with `gate`
// answering its knowledge condition -- or nullptr (stand idle). Pure, so tests
// can drive it with any clock and any knowledge.
const ScheduleEntry* activeEntry(const Config& cfg, double day_frac,
                                 const std::function<bool(const unlock::Condition&)>& gate);

struct Registry
{
    std::unordered_map<std::string, Config> npcs;
};

// Load every config/npcs/*.json into the registry (file stem = fallback id).
// Missing directory = empty registry, silently (a game with no NPCs yet is fine).
void load(Registry& reg, const std::string& dir);

// Spawn `cfg` standing at (x,y) world px, facing the cardinal `facing` (empty =
// south): idle pose, Y-sorted by feet, solid. Returns the entity.
entt::entity spawn(EntityManager& em, const Config& cfg, float x, float y,
                   const std::string& facing);

} // namespace npc
