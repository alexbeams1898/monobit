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
    const double frac = parseClockTime(loaded->value("start_time", std::string{}));
    clock.start_seconds = frac > 0.0 ? frac * clock.seconds_per_day : 0.0;
    if (const auto c = loaded->find("costs"); c != loaded->end() && c->is_object())
    {
        clock.costs.walk_minutes_per_100px =
            c->value("walk_minutes_per_100px", clock.costs.walk_minutes_per_100px);
        clock.costs.observe_minutes = c->value("observe_minutes", clock.costs.observe_minutes);
        clock.costs.deed_minutes = c->value("deed_minutes", clock.costs.deed_minutes);
        clock.costs.craft_minutes = c->value("craft_minutes", clock.costs.craft_minutes);
        clock.costs.warp_minutes = c->value("warp_minutes", clock.costs.warp_minutes);
    }
}

void tick(WorldClock& clock, double dt)
{
    clock.seconds += dt;
}

void advanceMinutes(WorldClock& clock, double minutes)
{
    if (minutes <= 0.0)
        return;
    clock.seconds += minutes * (clock.seconds_per_day / (24.0 * 60.0));
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
    // How far into its day the moment sits, on a 24-hour face. Rounded to the
    // nearest whole minute -- truncation would read a float-exact 10:47 as
    // 10:46 (46.999... minutes cut down).
    const double into =
        seconds - std::floor(seconds / clock.seconds_per_day) * clock.seconds_per_day;
    const long total = std::lround((into / clock.seconds_per_day) * 24.0 * 60.0) % (24 * 60);
    const int h = static_cast<int>(total / 60);
    const int m = static_cast<int>(total % 60);
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

double fractionOfDay(const WorldClock& clock)
{
    if (clock.seconds_per_day <= 0.0)
        return 0.0;
    const double into =
        clock.seconds - std::floor(clock.seconds / clock.seconds_per_day) * clock.seconds_per_day;
    return into / clock.seconds_per_day;
}

double parseClockTime(const std::string& hhmm)
{
    const std::size_t colon = hhmm.find(':');
    if (colon == std::string::npos || colon == 0 || colon + 1 >= hhmm.size())
        return -1.0;
    int h = 0;
    int m = 0;
    try
    {
        h = std::stoi(hhmm.substr(0, colon));
        m = std::stoi(hhmm.substr(colon + 1));
    }
    catch (...)
    {
        return -1.0;
    }
    if (h == 24 && m == 0)
        return 1.0; // "24:00" -- end of day, so an all-day window is authorable
    if (h < 0 || h > 23 || m < 0 || m > 59)
        return -1.0;
    return (h + m / 60.0) / 24.0;
}

bool inWindow(double frac, double from, double to)
{
    if (from <= to)
        return frac >= from && frac < to;
    return frac >= from || frac < to; // wraps midnight (a night window)
}

} // namespace worldclock
