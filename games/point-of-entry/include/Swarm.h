#pragma once

#include <string>
#include <vector>

class EntityManager;

// What comes up out of the ground, and when.
//
// Everything in this game festers up from the rot below and pushes through wherever the earth is
// weakest. A dug chamber has SEEPS -- the places it comes through -- authored as markers in the
// room templates, so where a floor bleeds is level data rather than a rule in code.
//
// The swarm arrives in WAVES rather than all at once: a wave is a breath, and the gap between
// them is when the player moves, reloads his nerve, and looks at the bar. Clear every wave and
// the space is his; until then it is not.
namespace swarm
{

// Where a chamber leaks. Placed from a room marker; enemies emerge here.
struct Seep
{
    float x = 0.0f;
    float y = 0.0f;
};

// How a floor's assault is shaped. Read from config, scaled by depth -- the same numbers
// growing is what makes a deeper dig worse without authoring each one by hand.
struct Assault
{
    int waves = 3;
    int per_wave = 6;      // at the first wave; each is bigger than the last
    float growth = 1.4f;   // multiplier per wave
    float spacing = 0.25f; // seconds between individual arrivals within a wave
    float breath = 3.0f;   // seconds of quiet between waves
};

// Where the fight is up to. One per dug chamber, not one per game.
enum class Phase
{
    Quiet,    // nothing has been disturbed yet
    Emerging, // a wave is coming up out of the ground
    Fighting, // everything in this wave is out; kill it
    Breath,   // wave cleared, the next is gathering
    Cleared,  // the whole assault is done -- the space is yours
};

// Begin the assault on this chamber. Called when the space is disturbed.
void begin(const Assault& assault, const std::vector<Seep>& seeps);

// Spawn, advance waves, notice when it is over.
void update(EntityManager& em, float dt);

Phase phase();
int waveNumber();
int totalWaves();
int remaining(const EntityManager& em);

// Load the assault shape for a given depth. Deeper is worse, from one set of curves.
Assault assaultForDepth(const std::string& configPath, int depth);

} // namespace swarm
