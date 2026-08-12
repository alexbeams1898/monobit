#pragma once

#include <string>

// ---------------------------------------------------------------------------
// CrashHandler -- the last thing that runs. A game that dies silently teaches
// nothing: the window vanishes, the terminal shows an exit code, and whatever
// was on the stack goes with it. This catches the ways a process can end
// without unwinding -- an access violation, a failed assert, an exception
// nobody caught, a std::terminate -- and writes WHERE it happened to the log
// the game already keeps, so a crash arrives as a stack trace rather than as a
// bug report that says "it closed".
//
// Engine-level because every game has the same need and the platform dance
// (unhandled-exception filter, signal handlers, symbol lookup) is worth
// writing once.
// ---------------------------------------------------------------------------

namespace engine::crash
{

// Catch what kills the process and write it down before it dies. `log_channel`
// names the channel to write into -- the same one the game logs to, so the
// crash lands at the end of the file that has the run leading up to it.
// Call once, as early in boot as possible: anything that crashes before this
// still crashes silently.
void install(const std::string& log_channel);

// The current call stack, innermost first, as text. Useful on its own for
// diagnosing a state that is wrong rather than fatal.
std::string stackTrace(int skip_frames = 0);

} // namespace engine::crash
