#pragma once

#include "UnlockCondition.h"

#include <string>
#include <unordered_map>
#include <vector>

#include <entt/entt.hpp>

// Scripted scenes: choreography that PLAYS the observation graph (see
// docs/design/GAME-SYSTEMS.md section 8). A scene moves bodies, holds player
// input, and fires EXISTING encounter content in order -- it never contains
// dialogue text of its own, only references. The world can force what he hears,
// never what he concludes: thoughts still roll through his faculties inside one.
namespace scene
{

// One step of choreography. Exactly one of the "verbs" is set (the loader keeps
// the first it finds); the rest of the fields parameterize it.
struct Step
{
    enum class Kind
    {
        Enter,     // an npc appears at a marker: {"enter": npc_id, "at": marker, "facing": dir}
        Leave,     // the npc departs (despawns): {"leave": npc_id}
        Move,      // walk an npc to a marker: {"move": npc_id, "to": marker}
        Face,      // turn an npc: {"face": npc_id, "dir": cardinal}
        Wait,      // hold for seconds: {"wait": 0.6}
        Observe,   // run an encounter's reading through the box: {"observe": encounter_id}
        Remark,    // force an authored remark to be said: {"remark": remark_id}
        Menu,      // open an encounter's deed menu (choices ARE deeds): {"menu": encounter_id}
        SetFlag,   // raise a flag through the knowledge engine: {"set_flag": name}
        Sound,     // start an ambience channel by name: {"sound": channel}
        StopSound, // stop one: {"stop_sound": channel}
        FadeIn     // the screen lightens from full black over seconds: {"fade_in": 4.0}
    };
    Kind kind = Kind::Wait;
    std::string who;      // npc id (Enter/Leave/Move/Face)
    std::string target;   // marker id (Enter/Move), encounter id (Observe/Menu), flag (SetFlag)
    std::string facing;   // cardinal (Enter/Face)
    float seconds = 0.0f; // Wait/FadeIn
    // Observe/Menu pacing. blocking=false hands the box its content and moves on --
    // a body keeps walking while its words are read (a menu still holds until the
    // CHOICE is made; only the reply text overlaps what follows). must_choose menus
    // offer no Leave: the deeds repeat until one consumes the spot -- a decision
    // the player cannot walk away from.
    bool blocking = true;
    bool must_choose = false;
};

// An authored scene (config/scenes/<id>.json). `level` is where it can start;
// `start_when` the knowledge it waits for (empty = as soon as you're there);
// `set_flag` is raised when it COMPLETES -- required, because it is also the
// once-guard: a scene whose flag is set never starts again, and later scenes
// sequence by naming it in their own start_when.
struct Def
{
    std::string id;
    std::string level;
    unlock::Condition start_when;
    std::string set_flag;
    std::vector<Step> steps;
};

struct Registry
{
    std::vector<Def> scenes;
};

// Load every config/scenes/*.json. Missing directory = empty registry.
void load(Registry& reg, const std::string& dir);

// The running scene, if any. Owned by GameState; ticked by the game loop (which
// holds player movement while `active`). `bodies` maps npc id -> the entity a
// scene Enter spawned, so Move/Face/Leave can find it.
struct Runtime
{
    bool active = false;
    const Def* def = nullptr; // points into the registry (stable for the walk)
    std::size_t step = 0;
    float timer = 0.0f;  // Wait countdown
    bool pushed = false; // Observe/Menu: content already fired, waiting on the box
    std::unordered_map<std::string, entt::entity> bodies;
};

} // namespace scene
