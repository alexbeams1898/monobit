#include "WorldClock.h"

#include <cmath>

namespace worldclock
{

void tick(WorldClock& clock, double dt)
{
    clock.seconds += dt;
}

int dayAt(const WorldClock& clock, double seconds)
{
    if (clock.seconds_per_day <= 0.0)
        return 1;
    return 1 + static_cast<int>(std::floor(seconds / clock.seconds_per_day));
}

int day(const WorldClock& clock)
{
    return dayAt(clock, clock.seconds);
}

std::string stamp(const WorldClock& clock)
{
    return "Day " + std::to_string(day(clock));
}

} // namespace worldclock
