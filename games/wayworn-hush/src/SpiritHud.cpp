#include "SpiritHud.h"

#include "HudCanvas.h"
#include "JsonConfig.h"
#include "UIRenderer.h"

#include <algorithm>
#include <cmath>

namespace spirit_hud
{
namespace
{
FontHandle sFont = -1;

// The number ON SCREEN, which lags the target -- see the header. -1 = never shown yet, so the
// first render snaps rather than climbing up from zero.
int sShown = -1;
// What the shown number is climbing TOWARD: the total as far as it has been announced, which
// trails the pilgrim's true total while lines are still queued to explain it.
int sTarget = 0;
// The gain announcing itself: how much, and how long it has been on screen. `sGainAge` past
// hold+fade means there is nothing to draw.
int sGainAmount = 0;
float sGainAge = 0.0f;
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
    cfg.catch_up = j.value("catch_up", cfg.catch_up);
    cfg.min_step = j.value("min_step", cfg.min_step);
    cfg.gain_hold = j.value("gain_hold", cfg.gain_hold);
    cfg.gain_fade = j.value("gain_fade", cfg.gain_fade);
    cfg.gain_rise = j.value("gain_rise", cfg.gain_rise);
    cfg.text = config::readColor(j, "text", cfg.text);
    cfg.gain = config::readColor(j, "gain", cfg.gain);
    cfg.panel = config::readColor(j, "panel", cfg.panel);
}

void init(FontHandle badge_font)
{
    sFont = badge_font;
}

void reset(int spirit)
{
    sShown = spirit;
    sTarget = spirit;
    sGainAmount = 0;
    sGainAge = 0.0f;
}

void gained(int amount)
{
    if (amount <= 0)
        return;
    // This much has now been EXPLAINED, so this much may feed in. A second gain replaces the
    // label rather than adding to it: each line's reward is its own, and the one before it has
    // already been credited to the target.
    sTarget += amount;
    sGainAmount = amount;
    sGainAge = 0.0f;
}

void tick(const Config& cfg, int spirit, bool settled, float dt)
{
    // Once nothing is waiting to be read, the target IS the truth: this catches anything the
    // world credited without a line to explain it (a deed's reward, a flag's cascade) and
    // corrects any drift, without ever letting the counter run ahead of the lines.
    if (settled)
        sTarget = spirit;
    // Spending, or a load: the truth dropping below what was announced is not a climb.
    if (sTarget > spirit)
        sTarget = spirit;

    // Climb toward the target. A fraction of the remaining gap each frame, so a big
    // understanding starts fast and eases in; `min_step` stops the tail crawling. A drop is
    // simply so, and snaps.
    if (sShown < 0 || sShown > sTarget)
        sShown = sTarget;
    else if (sShown < sTarget)
    {
        const int gap = sTarget - sShown;
        const int step = std::max(
            cfg.min_step, static_cast<int>(std::lround(static_cast<float>(gap) * cfg.catch_up)));
        sShown = std::min(sTarget, sShown + step);
    }

    // Age the "+N" out. It holds, then fades; render draws whatever is left of it.
    if (sGainAmount > 0)
    {
        sGainAge += dt;
        if (sGainAge > cfg.gain_hold + cfg.gain_fade)
            sGainAmount = 0;
    }
}

int shown()
{
    return sShown;
}

Rect bounds(const Config& cfg, int windowW, int windowH)
{
    if (sFont < 0)
        return {};
    const float s = hud::scale(windowW, windowH);
    const TextSize ts = UIRenderer::measureText(sFont, std::to_string(std::max(0, sShown)));
    const float pad = cfg.pad * s;
    return {cfg.x * static_cast<float>(windowW), cfg.y * static_cast<float>(windowH),
            ts.width + 2.0f * pad, ts.height + 2.0f * pad};
}

void render(const Config& cfg, int windowW, int windowH)
{
    if (sFont < 0)
        return;

    const float s = hud::scale(windowW, windowH);
    const std::string total = std::to_string(std::max(0, sShown));
    const TextSize ts = UIRenderer::measureText(sFont, total);
    const float pad = cfg.pad * s;
    const Rect box = bounds(cfg, windowW, windowH);
    const float bx = box.x;
    const float by = box.y;

    UIRenderer::drawRect(bx, by, box.w, box.h, cfg.panel);
    UIRenderer::drawText(sFont, total, bx + pad + 1.0f, by + pad + 1.0f, {0.0f, 0.0f, 0.0f, 0.5f});
    UIRenderer::drawText(sFont, total, bx + pad, by + pad, cfg.text);

    // The "+N" above the box: it holds, then fades and drifts up as the total absorbs it.
    if (sGainAmount <= 0)
        return;
    const float fading = std::max(0.0f, sGainAge - cfg.gain_hold);
    const float alpha = cfg.gain_fade > 0.0f ? std::max(0.0f, 1.0f - fading / cfg.gain_fade) : 0.0f;
    const std::string label = "+" + std::to_string(sGainAmount);
    const TextSize gs = UIRenderer::measureText(sFont, label);
    // Right-aligned with the total's right edge, so the two read as one column whatever the
    // digits do; lifted clear of the panel and drifting as it goes.
    const float gx = bx + pad + ts.width - gs.width;
    const float gy = by - gs.height - pad * 0.5f - fading * cfg.gain_rise * s;
    UIRenderer::drawText(sFont, label, gx + 1.0f, gy + 1.0f, {0.0f, 0.0f, 0.0f, 0.5f * alpha});
    UIRenderer::drawText(sFont, label, gx, gy, {cfg.gain.r, cfg.gain.g, cfg.gain.b, alpha});
}

} // namespace spirit_hud
