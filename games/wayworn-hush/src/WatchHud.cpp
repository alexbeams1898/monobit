#include "WatchHud.h"

#include "HudCanvas.h"
#include "JsonConfig.h"
#include "UIRenderer.h"

namespace watch_hud
{
namespace
{
FontHandle sFont = -1;
} // namespace

void load(Config& cfg, const std::string& path)
{
    const auto loaded = config::load(path);
    if (!loaded)
        return;
    const nlohmann::json& j = *loaded;
    cfg.x = j.value("x", cfg.x);
    cfg.y = j.value("y", cfg.y);
    cfg.pad = j.value("pad", cfg.pad);
    cfg.text = config::readColor(j, "text", cfg.text);
    cfg.panel = config::readColor(j, "panel", cfg.panel);
}

void init(FontHandle badge_font)
{
    sFont = badge_font;
}

void render(const Config& cfg, const worldclock::WorldClock& clock, bool carried, int windowW,
            int windowH)
{
    if (sFont < 0 || !carried)
        return; // no watch in the satchel -- nothing is telling him the time

    const std::string label = worldclock::stampAt(clock, clock.seconds);

    // hud::scale returns the safe-area WIDTH in px; authored sizes are canvas FRACTIONS
    // multiplied by it. The measured text is already in real px, so only `pad` gets scaled.
    const float s = hud::scale(windowW, windowH);
    const TextSize ts = UIRenderer::measureText(sFont, label);
    const float pad = cfg.pad * s;
    const float bx = cfg.x * static_cast<float>(windowW);
    const float by = cfg.y * static_cast<float>(windowH);

    UIRenderer::drawRect(bx, by, ts.width + 2.0f * pad, ts.height + 2.0f * pad, cfg.panel);
    // Soft shadow then the label -- the game's legible-over-world idiom.
    UIRenderer::drawText(sFont, label, bx + pad + 1.0f, by + pad + 1.0f, {0.0f, 0.0f, 0.0f, 0.5f});
    UIRenderer::drawText(sFont, label, bx + pad, by + pad, cfg.text);
}

} // namespace watch_hud
