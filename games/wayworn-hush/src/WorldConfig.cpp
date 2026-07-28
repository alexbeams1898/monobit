#include "WorldConfig.h"

#include "JsonConfig.h"

namespace world_config
{

void load(Config& cfg, const std::string& path)
{
    const auto loaded = config::load(path);
    if (!loaded)
        return;
    const nlohmann::json& j = *loaded;
    cfg.ldtk = j.value("ldtk", cfg.ldtk);
    cfg.tileset_png = j.value("tileset_png", cfg.tileset_png);
    cfg.tileset_name = j.value("tileset_name", cfg.tileset_name);
    cfg.ambient_track = j.value("ambient_track", cfg.ambient_track);
    cfg.ambient_volume = j.value("ambient_volume", cfg.ambient_volume);
    cfg.ambient_fade_in_ms = j.value("ambient_fade_in_ms", cfg.ambient_fade_in_ms);
    cfg.ambient_gate_flag = j.value("ambient_gate_flag", cfg.ambient_gate_flag);
    cfg.warp_fade_seconds = j.value("warp_fade_seconds", cfg.warp_fade_seconds);
    cfg.warp_enter_sfx = j.value("warp_enter_sfx", cfg.warp_enter_sfx);
    cfg.warp_exit_sfx = j.value("warp_exit_sfx", cfg.warp_exit_sfx);
    cfg.warp_sfx_volume = j.value("warp_sfx_volume", cfg.warp_sfx_volume);
}

} // namespace world_config
