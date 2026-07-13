#include "WorldClock.h"

#include <cmath>

namespace worldclock
{

void tick(WorldClock& clock, double dt)
{
    clock.seconds += dt;
}

int day(const WorldClock& clock)
{
    if (clock.seconds_per_day <= 0.0)
        return 1;
    return 1 + static_cast<int>(std::floor(clock.seconds / clock.seconds_per_day));
}

std::string stamp(const WorldClock& clock)
{
    return "Day " + std::to_string(day(clock));
}

} // namespace worldclock
