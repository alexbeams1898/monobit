#pragma once

// What the program is doing, as opposed to what the world is doing.
//
// A plain value, not a scene hierarchy: transitions are assignments, and the phase is the one
// thing that gates whether the world ticks at all. The pause screen layers ON TOP of Playing
// rather than being a phase of its own -- pausing is something the world does, not something
// the program becomes.
//
// The world is not built until the player commits to entering it, so the title does not need
// a generated floor behind it.
namespace app
{

enum class Phase
{
    Title,    // the shell: take the job, carry on, settings, or leave
    Settings, // how you like the game -- reachable from the title AND from a pause
    Playing   // in the house
};

struct State
{
    Phase phase = Phase::Title;
    // Where leaving Settings returns to. Settings is the one phase with two ways in, so it is
    // the one that has to remember. A field rather than a stack: this is a menu, not a
    // browser, and one honest field beats a general history nothing else needs.
    Phase settings_return_to = Phase::Title;
    // The world exists (floor generated, player spawned). False at the title until a commit
    // builds it; anything touching the world checks this rather than assuming it is there.
    bool world_built = false;
    // Whether the pause screen is up. Layered over Playing, so it is a flag rather than a
    // phase -- the world is still loaded, it is simply not ticking.
    bool paused = false;
};

} // namespace app
