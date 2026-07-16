#include "WorldClock.h"

#include "JsonConfig.h"

#include <cmath>

namespace worldclock
{

void load(WorldClock& clock, const std::string& path)
{
    const auto loaded = config::load(path);
    if (!loaded)
        return;
    clock.seconds_per_day = loaded->value("seconds_per_day", clock.seconds_per_day);
}

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

std::string timeAt(const WorldClock& clock, double seconds)
{
    if (clock.seconds_per_day <= 0.0)
        return "";
    // How far into its day the moment sits, on a 24-hour face.
    const double into =
        seconds - std::floor(seconds / clock.seconds_per_day) * clock.seconds_per_day;
    const double hours = (into / clock.seconds_per_day) * 24.0;
    const int h = static_cast<int>(hours);
    const int m = static_cast<int>((hours - h) * 60.0);
    const std::string mm = (m < 10 ? "0" : "") + std::to_string(m);
    return std::to_string(h) + ":" + mm;
}

std::string stampAt(const WorldClock& clock, double seconds)
{
    const std::string dayPart = "Day " + std::to_string(dayAt(clock, seconds));
    const std::string timePart = timeAt(clock, seconds);
    // A clock with no cadence tells no hour -- say the day alone rather than "Day 1, " with
    // a comma dangling where the time should be.
    return timePart.empty() ? dayPart : dayPart + ", " + timePart;
}

} // namespace worldclock
