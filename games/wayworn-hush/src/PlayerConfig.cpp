#include "PlayerConfig.h"

#include "JsonConfig.h"

#include <nlohmann/json.hpp>

namespace
{
PlayerConfig::AnimState parseState(const nlohmann::json& j, const PlayerConfig::AnimState& def)
{
    PlayerConfig::AnimState s;
    s.row = j.value("row", def.row);
    s.frames = j.value("frames", def.frames);
    s.duration = j.value("duration", def.duration);
    return s;
}
} // namespace

PlayerConfig loadPlayerConfig(const std::string& path)
{
    PlayerConfig cfg;
    const auto loaded = config::load(path);
    if (!loaded)
        return cfg;
    const nlohmann::json& j = *loaded;

    cfg.speed = j.value("speed", cfg.speed);
    cfg.run_speed_mult = j.value("run_speed_mult", cfg.run_speed_mult);
    if (const auto c = j.find("collider"); c != j.end() && c->is_object())
    {
        cfg.collider_w = c->value("w", cfg.collider_w);
        cfg.collider_h = c->value("h", cfg.collider_h);
    }
    if (const auto it = j.find("animation"); it != j.end())
    {
        const auto& a = *it;
        cfg.texture = a.value("texture", cfg.texture);
        cfg.frame_width = a.value("frame_width", cfg.frame_width);
        cfg.frame_height = a.value("frame_height", cfg.frame_height);
        cfg.direction_count = a.value("direction_count", cfg.direction_count);
        cfg.max_frames_per_state = a.value("max_frames_per_state", cfg.max_frames_per_state);
        if (const auto st = a.find("states"); st != a.end())
        {
            if (const auto i = st->find("idle"); i != st->end())
                cfg.idle = parseState(*i, cfg.idle);
            if (const auto w = st->find("walk"); w != st->end())
                cfg.walk = parseState(*w, cfg.walk);
            if (const auto fw = st->find("fast_walk"); fw != st->end())
                cfg.fast_walk = parseState(*fw, cfg.fast_walk);
        }
    }
    return cfg;
}
