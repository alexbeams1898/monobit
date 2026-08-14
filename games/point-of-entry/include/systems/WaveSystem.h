#pragma once

#include <string>
#include <vector>

#include <entt/fwd.hpp>

class EntityManager;

// The holes, each running its OWN program.
//
// A hole is not a lottery ticket -- it is a typed entry point (config/holes/) declaring what
// can come through it, how its waves run, and what it looks like. Every hole on the floor
// schedules independently, so pressure arrives staggered from different bearings and no single
// held cone answers the floor.
//
// THE LAW OF DEPTH: proximity to the source dictates difficulty. Depth is the only dial --
// it scales wave sizes through the assault curves, adds each species' growth spread to its
// stat sheet, and runs THE SMELL hotter: the per-individual roll that multiplies every
// derived number and, hot enough, surfaces the species' evolved form instead. All of it
// reads one config surface (swarm.json curves + field guide formulas + pest files);
// nothing scales per-case.
namespace swarm
{

// One placed hole: where, what KIND (a hole file path), and WHOSE floor's program it runs.
//
// Depth is the hole's own because a hole does not stop belonging to its floor when it reaches
// him somewhere else: what comes up a way down is the floor below still pressing, at its own
// difficulty and out of its own finite program. A hole on the floor he is standing on simply
// has that floor's depth.
struct Hole
{
    float x = 0.0f;
    float y = 0.0f;
    std::string type;
    int depth = 0;
};

// Where the whole floor's fight is up to -- the aggregate over every hole's own program.
enum class Phase
{
    Quiet,    // nothing disturbed yet
    Emerging, // at least one hole is producing
    Fighting, // everything scheduled is out; kill it
    Breath,   // every hole is between waves
    Cleared,  // every program finished and everything dead
};

// Begin the floor's assault: each hole starts its own program, shaped by
// depth. A hole whose index is in `cleared` starts SPENT -- its program
// already exhausted on an earlier visit; the source does not re-press a
// finished hole.
// A hole not in `opened` starts SEALED: its program exists but does not run, because a floor
// answers being disturbed rather than being walked into.
void begin(const std::string& configPath, const std::vector<Hole>& holes, int depth,
           const std::vector<bool>& cleared = {}, const std::vector<int>& killed = {},
           const std::vector<bool>& opened = {});

// Point a hole at a different hole's program mid-floor, fast-forwarded past what has already
// been killed out of it. An empty type seals it instead. This is what lets ONE passage carry
// the floors below it in turn: when the hole it is running exhausts, the next takes its place
// without disturbing anything else on the floor.
void retarget(int holeIndex, const Hole& to, int killed);

// Break a sealed hole open -- from here it presses.
void wake(int holeIndex);

// Still sealed? A sealed hole is not work and not a way down; it is a question.
bool holeSealed(int holeIndex);

// How far through its program one hole is, for a readout: the wave it is on and how many it
// has. Wave 0 means it has not started one yet.
int holeWave(int holeIndex);
int holeWaves(int holeIndex);

// KILLING IS THE ONLY PROGRESS. A hole's program is a fixed number of pests;
// what has been killed out of it is remembered per hole and the program resumes
// past it, so leaving a floor -- by the stairs, by dying, by quitting -- costs
// nothing and gains nothing. Anything that emerged and was NOT killed simply
// comes up again, which is what makes walking out and back in worth no XP.
void countKill(const EntityManager& em, entt::entity dead);
const std::vector<int>& progress();

// Has this hole's program exhausted with nothing of its output left standing?
// The moment it flips true, the hole stops being a spawner and can become a
// way down.
bool holeCleared(const EntityManager& em, int holeIndex);

// One pest out of a hole, outside any wave program -- what a dig site
// leaks in the authored world. The full emergence recipe, at current depth.
entt::entity spawnOne(EntityManager& em, const std::string& pestPath, float x, float y);

void update(EntityManager& em, float dt);

// The aggregate over every hole's own program -- the floor's state, not any one hole's.
Phase phase();
int remaining(const EntityManager& em);

} // namespace swarm
