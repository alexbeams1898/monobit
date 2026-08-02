#include "Formulas.h"

#include "JsonConfig.h"

#include <algorithm>

namespace formulas
{

void load(Config& cfg, const std::string& path)
{
    const auto loaded = config::load(path);
    if (!loaded)
        return;
    const nlohmann::json& j = *loaded;
    if (const auto g = j.find("glow"); g != j.end() && g->is_object())
    {
        cfg.glow.max = g->value("max", cfg.glow.max);
        cfg.glow.half_at = g->value("half_at", cfg.glow.half_at);
    }
}

float glowBrightness(const Config& cfg, int perception)
{
    // Soft cap: max * P / (P + half_at). Approaches max asymptotically; hits half of max at
    // P == half_at. Perception 0 -> 0 (a quiet world). half_at <= 0 falls back to full.
    const float p = static_cast<float>(std::max(0, perception));
    if (cfg.glow.half_at <= 0.0f)
        return cfg.glow.max;
    return cfg.glow.max * p / (p + cfg.glow.half_at);
}

} // namespace formulas
