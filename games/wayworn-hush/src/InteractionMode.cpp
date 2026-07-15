#include "InteractionMode.h"

#include "HudCanvas.h"
#include "JsonConfig.h"
#include "UIRenderer.h"
#include "systems/AudioSystem.h"

namespace interaction_mode
{
namespace
{
FontHandle sFont = -1;

Color readColor(const nlohmann::json& j, const char* key, const Color& fallback)
{
    const auto it = j.find(key);
    if (it == j.end() || !it->is_array() || it->size() != 4)
        return fallback;
    return {(*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>(),
            (*it)[3].get<float>()};
}
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
    cfg.observe_label = j.value("observe_label", cfg.observe_label);
    cfg.act_label = j.value("act_label", cfg.act_label);
    cfg.observe_color = readColor(j, "observe_color", cfg.observe_color);
    cfg.act_color = readColor(j, "act_color", cfg.act_color);
    cfg.panel = readColor(j, "panel", cfg.panel);
    cfg.enter_act_sound = j.value("enter_act_sound", cfg.enter_act_sound);
    cfg.enter_observe_sound = j.value("enter_observe_sound", cfg.enter_observe_sound);
    cfg.sfx_volume = j.value("sfx_volume", cfg.sfx_volume);
}

void init(FontHandle badge_font)
{
    sFont = badge_font;
}

void update(State& state, const Config& cfg, bool running)
{
    // First frame: adopt the stance silently (no transition SFX on startup).
    if (!state.primed)
    {
        state.acting = running;
        state.primed = true;
        return;
    }
    if (running == state.acting)
        return; // no change
    state.acting = running;
    // Distinct cue per direction: enter-Act (Shift down) vs return-to-Observe (Shift up). The
    // placeholder sounds share a file; a pitch split keeps the two directions audibly distinct
    // until final cues are authored (Act higher/firmer, Observe lower/softer).
    if (running)
        AudioSystem::playSfx(cfg.enter_act_sound, cfg.sfx_volume, 1.18f);
    else
        AudioSystem::playSfx(cfg.enter_observe_sound, cfg.sfx_volume, 0.85f);
}

void render(const State& state, const Config& cfg, int windowW, int windowH)
{
    if (sFont < 0)
        return;
    const std::string& label = state.acting ? cfg.act_label : cfg.observe_label;
    const Color& fg = state.acting ? cfg.act_color : cfg.observe_color;

    // hud::scale returns the safe-area WIDTH in px; authored sizes are canvas FRACTIONS
    // multiplied by it. The measured text is already in real px, so only `pad` (a fraction)
    // gets scaled.
    const float s = hud::scale(windowW, windowH);
    const TextSize ts = UIRenderer::measureText(sFont, label);
    const float pad = cfg.pad * s;
    const float bx = cfg.x * static_cast<float>(windowW);
    const float by = cfg.y * static_cast<float>(windowH);
    const float bw = ts.width + 2.0f * pad;
    const float bh = ts.height + 2.0f * pad;

    UIRenderer::drawRect(bx, by, bw, bh, cfg.panel);
    // Soft shadow then the label (mirrors PausePage::softText).
    UIRenderer::drawText(sFont, label, bx + pad + 1.0f, by + pad + 1.0f, {0.0f, 0.0f, 0.0f, 0.5f});
    UIRenderer::drawText(sFont, label, bx + pad, by + pad, fg);
}

} // namespace interaction_mode
