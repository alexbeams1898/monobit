#pragma once

#include <functional>
#include <string>

// In-engine crash handler. Catches the three classes of fatal failure
// that would otherwise kill the process silently with no signal to
// the player and no diagnostic for us:
//
//   1. Windows SEH (access violations, divide-by-zero, stack overflow,
//      illegal instruction). The textbook silent-segfault case.
//   2. Uncaught C++ exceptions / std::terminate.
//   3. C++ exceptions thrown out of the main loop body.
//
// On catch, the handler:
//   - Writes a diagnostic dump to <save-dir>/crashes/crash-<ts>.log
//     (stack trace via DbgHelp on Windows, plus context: phase, region,
//     last N log lines, build version).
//   - Writes a one-line summary to <save-dir>/crashes/last_crash_summary.txt
//     so the next boot can show a main-menu banner.
//   - Invokes the user-supplied save_cb (emergency-save active profile).
//   - Invokes the user-supplied teardown_cb (currently unused; reserved
//     for future audio/output flush before exit).
//   - Process exits via OS unwinder (EXCEPTION_EXECUTE_HANDLER) or
//     TerminateProcess. NO recovery attempt -- after a fatal fault
//     the process state is unknown and continuing risks save
//     corruption. Industry standard pattern (Unreal/Unity/id Tech/
//     Source all do dump-then-die).
//
// An earlier design attempted in-engine recovery via VEH RIP/RSP
// rewriting. It infinite-looped under fault because RSP can't be
// reliably restored to a still-valid main-loop frame without
// __try/__except keywords (which MinGW G++/clang reject). Removed.
//
// The handler runs in both debug and release builds -- silent crashes
// are exactly the failure mode we're solving, so withholding the
// handler from release would defeat the purpose. Stack symbols are
// best-effort: full in debug (PDB shipped), addresses + module names
// in release.
//
// Threading: the handler is installed once at boot and never
// reinstalled. The save/teardown callbacks run on the thread the
// crash occurred on (typically the main loop thread). They MUST be
// safe to call from a potentially inconsistent state -- audit them
// for "won't recursively crash if the world is half-torn-down."

namespace engine
{

// Callback invoked from the handler to attempt an emergency save of
// the active profile. Wrapped in its own try/catch internally -- a
// crash during emergency save will NOT mask the original crash.
// Return true on successful save, false if the state was too
// inconsistent to risk it.
using SaveOnCrashFn = std::function<bool()>;

// Callback invoked from the handler to tear gameplay down. After this
// returns, the main loop will resume from its recovery branch (i.e.
// the game should be in the main menu, with no live region / actors
// / audio that could still reference torn-down state).
using TeardownOnCrashFn = std::function<void()>;

// Install handlers. Call ONCE, very early in main() -- before any
// subsystem init -- so we can catch init crashes too. Idempotent
// (second call is a no-op + logs a warning). `save_cb` and
// `teardown_cb` may be null for the early-install case; call again
// after subsystems are up to populate them. Crashes between the
// early install and the populated install will still get dumped,
// just without save/teardown.
void installCrashHandler(SaveOnCrashFn save_cb, TeardownOnCrashFn teardown_cb);

// Replace the save/teardown callbacks after install. Used when the
// early install ran with null callbacks and the game-side wiring
// is now ready to provide them.
void setCrashRecoveryCallbacks(SaveOnCrashFn save_cb, TeardownOnCrashFn teardown_cb);

// Per-frame OS-handler refresh. Call at the top of every main-loop
// iteration. Re-installs our SetUnhandledExceptionFilter +
// std::set_terminate so any third-party library that grabbed
// those slots after our boot install gets overridden back within
// one frame. Cheap (atomic OS-handle stores).
void armCrashRecovery();

// SEH-mode crash entry point. Public surface for tests + synthetic
// triggers. Production code path is OS-driven (SetUnhandledExceptionFilter).
//
// Argument is the OS's EXCEPTION_POINTERS struct, type-erased so
// the header doesn't pull in windows.h. Pass nullptr if no info.
void handleSehCrash(void* exception_pointers);

// True if a crash dump was found in the crashes directory at boot
// (i.e. the previous run crashed). UI reads this to show the
// main-menu recovery banner. Call clearLastCrashSummary() after the
// player has seen it.
bool hadPreviousCrash();

// The contents of last_crash_summary.txt from the prior run (empty
// if hadPreviousCrash() is false). One short line describing what
// crashed -- shown verbatim in the banner.
const std::string& lastCrashSummary();

// Delete last_crash_summary.txt so the banner doesn't show again
// on subsequent boots until a new crash occurs.
void clearLastCrashSummary();

// Set the save directory the handler should write dumps + summaries
// into. Call once at boot after the game-side has resolved the
// per-platform save path. Default if unset: "./crashes/" (cwd).
void setCrashSaveDirectory(const std::string& dir);

// Update the "context" string the handler will include in dumps. Game
// code calls this from key transition points (phase change, region
// enter, character load) so the dump answers "what was going on?"
// without us having to reach into game state from the handler. Cheap
// -- writes to a pre-allocated buffer.
void setCrashContext(const std::string& context);

// RAII helper: set a contextual "what subsystem am I in?" string on
// entry, restore the previous context on exit. Used to narrow the
// crash dump's Context: line to the subsystem that was active when
// the fault hit. Pattern:
//
//   {
//       engine::CrashContextScope ctx("renderPickupMeshes");
//       selva::render::renderPickupMeshes();
//   }
//
// Trivially cheap (two snprintf-class writes per frame per scope);
// only matters when a crash happens, and then it's gold -- the
// dump's Context line goes from "(none)" to "renderPickupMeshes".
class CrashContextScope
{
  public:
    explicit CrashContextScope(const char* context);
    ~CrashContextScope();
    CrashContextScope(const CrashContextScope&) = delete;
    CrashContextScope& operator=(const CrashContextScope&) = delete;

  private:
    std::string prev_;
};

} // namespace engine
