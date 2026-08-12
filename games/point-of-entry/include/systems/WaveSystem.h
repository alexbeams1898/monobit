#pragma once

#include <string>
#include <vector>

#include <entt/fwd.hpp>

class EntityManager;

// The holes, each running its OWN program.
//
// A seep is not a lottery ticket -- it is a typed entry point (config/seeps/) declaring what
// can come through it, how its waves run, and what it looks like. Every seep on the floor
// schedules independently, so pressure arrives staggered from different bearings and no single
// held cone answers the floor.
//
// THE LAW OF DEPTH: proximity to the source dictates difficulty. Depth is the only dial --
// it scales wave sizes through the assault curves, adds each species' growth spread to its
// stat sheet, and runs THE SMELL hotter: the per-individual roll that multiplies every
// derived number and, hot enough, surfaces the species' evolved form instead. All of it
// reads one config surface (swarm.json curves + bestiary formulas + creature files);
// nothing scales per-case.
namespace swarm
{

// One placed hole: where, and what KIND (a seep file path).
struct Seep
{
    float x = 0.0f;
    float y = 0.0f;
    std::string type;
};

// Where the whole floor's fight is up to -- the aggregate over every seep's own program.
enum class Phase
{
    Quiet,    // nothing disturbed yet
    Emerging, // at least one hole is producing
    Fighting, // everything scheduled is out; kill it
    Breath,   // every hole is between waves
    Cleared,  // every program finished and everything dead
};

// Begin the floor's assault: each seep starts its own program, shaped by
// depth. A seep whose index is in `cleared` starts SPENT -- its program
// already exhausted on an earlier visit; the source does not re-press a
// finished hole.
void begin(const std::string& configPath, const std::vector<Seep>& seeps, int depth,
           const std::vector<bool>& cleared = {}, const std::vector<int>& killed = {});

// KILLING IS THE ONLY PROGRESS. A hole's program is a fixed number of creatures;
// what has been killed out of it is remembered per hole and the program resumes
// past it, so leaving a floor -- by the stairs, by dying, by quitting -- costs
// nothing and gains nothing. Anything that emerged and was NOT killed simply
// comes up again, which is what makes walking out and back in worth no XP.
void countKill(const EntityManager& em, entt::entity dead);
const std::vector<int>& progress();

// Has this hole's program exhausted with nothing of its output left standing?
// The moment it flips true, the hole stops being a spawner and can become a
// way down.
bool seepCleared(const EntityManager& em, int seepIndex);

// One creature out of a hole, outside any wave program -- what a dig site
// leaks in the authored world. The full emergence recipe, at current depth.
void spawnOne(EntityManager& em, const std::string& creaturePath, float x, float y);

void update(EntityManager& em, float dt);

// The aggregate over every hole's own program -- the floor's state, not any one hole's.
Phase phase();
int remaining(const EntityManager& em);

} // namespace swarm
