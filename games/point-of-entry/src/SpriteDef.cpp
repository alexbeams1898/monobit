#include "SpriteDef.h"

#include "Log.h"

#include <nlohmann/json.hpp>

#include <fstream>

namespace sprite_def
{

Def load(const std::string& path)
{
    Def d;
    std::ifstream in(path);
    if (!in)
    {
        poe::log().error("sprite: no def at '{}' -- run tools/art.py", path);
        return d;
    }
    const nlohmann::json j = nlohmann::json::parse(in, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded())
    {
        poe::log().error("sprite: '{}' is not valid JSON", path);
        return d;
    }

    d.sheet = j.value("sheet", std::string{});
    d.frame_w = j.value("frame_w", 0);
    d.frame_h = j.value("frame_h", 0);
    d.frames = j.value("frames", 0);
    d.anchor_x = j.value("anchor_x", -1);
    d.anchor_y = j.value("anchor_y", -1);
    if (const auto it = j.find("durations"); it != j.end() && it->is_array())
        for (const auto& v : *it)
            d.durations_ms.push_back(v.get<int>());
    if (const auto it = j.find("anims"); it != j.end() && it->is_object())
        for (const auto& [name, range] : it->items())
            d.anims.push_back(Anim{name, range.value("from", 0), range.value("to", 0)});

    d.ok = !d.sheet.empty() && d.frame_w > 0 && d.frame_h > 0;
    if (!d.ok)
        poe::log().error("sprite: '{}' is missing a sheet or a frame size", path);
    return d;
}

} // namespace sprite_def
