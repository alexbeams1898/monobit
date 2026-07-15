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
}

} // namespace world_config
