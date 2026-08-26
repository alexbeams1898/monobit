#include "ecs/FeelConfig.h"

#include "ops/LogUtils.h"

#include <nlohmann/json.hpp>

#include <fstream>

namespace feel
{
namespace
{
Numbers sNumbers;

// A block's worth, or nothing if the document does not carry it.
nlohmann::json block(const nlohmann::json& j, const char* name)
{
    return j.value(name, nlohmann::json::object());
}
} // namespace

bool load(const std::string& path)
{
    std::ifstream in(path);
    if (!in)
    {
        poe::log().warn("feel: no config at '{}' -- using the built-in numbers", path);
        return false;
    }
    const nlohmann::json j = nlohmann::json::parse(in, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded())
    {
        poe::log().error("feel: '{}' is not valid JSON -- using the built-in numbers", path);
        return false;
    }

    const auto aim = block(j, "aim");
    sNumbers.aim.dead_zone = aim.value("dead_zone", sNumbers.aim.dead_zone);

    const auto walk = block(j, "walk");
    sNumbers.walk.stride = walk.value("stride", sNumbers.walk.stride);
    sNumbers.walk.hop = walk.value("hop", sNumbers.walk.hop);
    sNumbers.walk.tilt = walk.value("tilt", sNumbers.walk.tilt);
    sNumbers.walk.poses_per_step = walk.value("poses_per_step", sNumbers.walk.poses_per_step);
    sNumbers.walk.inset = walk.value("inset", sNumbers.walk.inset);
    sNumbers.walk.brace_time = walk.value("brace_time", sNumbers.walk.brace_time);

    const auto contact = block(j, "contact");
    sNumbers.contact.interval = contact.value("interval", sNumbers.contact.interval);
    sNumbers.contact.radius = contact.value("radius", sNumbers.contact.radius);
    sNumbers.contact.spacing = contact.value("spacing", sNumbers.contact.spacing);

    const auto wand = block(j, "wand");
    sNumbers.wand.resume_seconds = wand.value("resume_seconds", sNumbers.wand.resume_seconds);

    const auto fade = block(j, "fade");
    sNumbers.fade.death = fade.value("death", sNumbers.fade.death);
    sNumbers.fade.travel = fade.value("travel", sNumbers.fade.travel);

    const auto reach = block(j, "reach");
    sNumbers.reach.pickup = reach.value("pickup", sNumbers.reach.pickup);
    sNumbers.reach.passage = reach.value("passage", sNumbers.reach.passage);

    const auto reek = block(j, "reek");
    sNumbers.reek.every = reek.value("every", sNumbers.reek.every);
    sNumbers.reek.rise = reek.value("rise", sNumbers.reek.rise);
    sNumbers.reek.drift = reek.value("drift", sNumbers.reek.drift);
    sNumbers.reek.mouth = reek.value("mouth", sNumbers.reek.mouth);
    sNumbers.reek.stand = reek.value("stand", sNumbers.reek.stand);
    return true;
}

const Numbers& current()
{
    return sNumbers;
}

} // namespace feel
