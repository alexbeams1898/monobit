#pragma once

#include "log/Log.h"

// The game's one log channel. Everything goes to point-of-entry.log next to the
// exe (truncated per run) and is mirrored to stderr, so a run leaves a readable
// record without anyone having to reproduce a bug live.
//
// Use it: poe::log().info("floor: {} rooms, {} markers", rooms, markers);
//
// Levels, so a noisy diagnostic can be left in place rather than deleted:
//   trace  per-frame detail (off unless hunting something)
//   debug  what a system decided, once per event
//   info   lifecycle -- boot, floor built, level entered
//   warn   recoverable: a missing config, a room that would not fit
//   error  it did not work and the game cannot do the thing
namespace poe
{

// The channel, created on first use.
inline engine::log::Channel& log()
{
    return engine::log::Channel::get("poe", engine::log::Sink::File, "point-of-entry.log",
                                     engine::log::Level::Trace, /*mirror_to_stderr=*/true);
}

} // namespace poe
