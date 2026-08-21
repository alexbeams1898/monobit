#pragma once

#include <string>

// THE SOUND BANK -- sounds that belong to the GAME rather than to a thing in it.
//
// A name is a promise: one sound means one thing everywhere it plays. That is what makes a menu
// feel like one menu, and it is why a shared bank exists at all rather than each caller naming
// a file. Callers ask for "footstep", never for a path.
//
// A sound that belongs to a THING is not here -- a tool's spray rides config/tools.json, and a
// pest's voice will ride its own file, so that adding the thing brings its sound with it.
namespace sound
{

// Read config/audio.json. Missing or malformed leaves the bank empty and every play a no-op,
// which is the same way the engine treats a machine with no audio device: silent, not broken.
bool load(const std::string& path = "config/audio.json");

// Play a named sound. Entries with variations rotate at random, so a sound repeated quickly --
// footfalls, hits -- does not machine-gun. An unknown name logs once and is then silent, because
// a missing sound should be findable but must not spam a frame loop.
void play(const std::string& name);

} // namespace sound
