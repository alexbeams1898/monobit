#pragma once

#include <string>
#include <vector>

class EntityManager;

// The holes, each running its OWN program.
//
// A seep is not a lottery ticket -- it is a typed entry point (config/seeps/) declaring what
// can come through it, how its waves run, and what it looks like. Every seep on the floor
// schedules independently, so pressure arrives staggered from different bearings and no single
// held cone answers the floor.
//
// THE LAW OF DEPTH: proximity to the source dictates difficulty. Depth is the only dial --
// it scales wave sizes through the assault curves, JUICES what emerges (the same species,
// multiplied), and past a species' threshold sends its evolved form instead. All of it reads
// one config surface (swarm.json curves + creature files); nothing scales per-case.
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
           const std::vector<bool>& cleared = {});

// Has this hole's program exhausted with nothing of its output left standing?
// The moment it flips true, the hole stops being a spawner and can become a
// way down.
bool seepCleared(const EntityManager& em, int seepIndex);

// The same floor over again -- what dying costs.
void restart();

// One creature out of a hole, outside any wave program -- what a dig site
// leaks in the authored world. The full emergence recipe, at current depth.
void spawnOne(EntityManager& em, const std::string& creaturePath, float x, float y);

void update(EntityManager& em, float dt);

Phase phase();
int waveNumber(); // the furthest wave any hole has reached
int totalWaves(); // the longest program on the floor
int remaining(const EntityManager& em);

} // namespace swarm
