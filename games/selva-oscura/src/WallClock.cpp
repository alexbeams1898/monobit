#include "WallClock.h"

namespace selva
{

namespace
{
float sWallClockSeconds = 0.0f;
}

float wallClock()
{
    return sWallClockSeconds;
}

void advanceWallClock(float dt)
{
    sWallClockSeconds += dt;
}

} // namespace selva
