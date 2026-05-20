#pragma once

namespace selva
{

// Monotonic wall-clock seconds since game start. Updated once per
// frame at the top of selvaPerFrame via advanceWallClock(dt). Read
// from many places (combat windows, lockouts, debouncing, debug
// timestamps); this is the single source of truth.
//
// Cross-cutting concern by design — owning it in any one feature
// module would make every other feature reach across boundaries to
// read it. Free functions on a hidden static keep the surface
// minimal.
float wallClock();
void advanceWallClock(float dt);

} // namespace selva
