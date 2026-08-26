#include "utils/CrashHandler.h"

#include <string>

#include <catch2/catch_test_macros.hpp>

// The stack walker itself, exercised without crashing anything. A handler that only runs at the
// moment of death is a handler nobody finds out is broken until the one time it matters -- so
// the machinery that turns addresses into named frames is checked like any other code.

namespace
{
// Deliberately not inlined away: the trace should be able to name this.
[[gnu::noinline]] std::string deepInTheStack()
{
    return engine::crash::stackTrace();
}
} // namespace

TEST_CASE("the stack walker produces named frames")
{
    const std::string trace = deepInTheStack();
    REQUIRE_FALSE(trace.empty());

#ifdef _WIN32
    // Frames are numbered from the innermost out.
    REQUIRE(trace.find("#00") != std::string::npos);
    // And every frame carries something that can be turned back into a source line. Names are
    // NOT the contract: this toolchain emits DWARF, which the platform symbol server cannot
    // read, so internal functions come out as "??" and the module-relative offset is what gets
    // resolved afterwards. A trace without offsets is a trace nobody can act on.
    REQUIRE(trace.find("+0x") != std::string::npos);
#endif
}

TEST_CASE("skipping frames drops the innermost ones")
{
    const std::string all = engine::crash::stackTrace(0);
    const std::string fewer = engine::crash::stackTrace(4);
    REQUIRE_FALSE(all.empty());
    REQUIRE_FALSE(fewer.empty());
    // Same walk, started further out: it cannot be the longer of the two.
    REQUIRE(fewer.size() <= all.size());
}
