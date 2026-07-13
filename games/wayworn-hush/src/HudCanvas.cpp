#include "HudCanvas.h"

#include "UIRenderer.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>

namespace hud
{
namespace
{
constexpr float kAspect = 16.0f / 9.0f; // reference canvas aspect
}

Rect safeArea(int windowW, int windowH)
{
    const float ww = static_cast<float>(windowW);
    const float wh = static_cast<float>(windowH);
    // Fit a 16:9 rect inside the window: width-bound if the window is wider than
    // 16:9 (pillarbox), height-bound if taller (letterbox). Centered.
    float w = ww;
    float h = ww / kAspect;
    if (h > wh)
    {
        h = wh;
        w = wh * kAspect;
    }
    return {(ww - w) * 0.5f, (wh - h) * 0.5f, w, h};
}

float x(float fx, int windowW, int windowH)
{
    const Rect s = safeArea(windowW, windowH);
    return s.x + fx * s.w;
}

float y(float fy, int windowW, int windowH)
{
    const Rect s = safeArea(windowW, windowH);
    return s.y + fy * s.h;
}

Rect rect(float fx, float fy, float fw, float fh, int windowW, int windowH)
{
    const Rect s = safeArea(windowW, windowH);
    return {s.x + fx * s.w, s.y + fy * s.h, fw * s.w, fh * s.h};
}

float scale(int windowW, int windowH)
{
    // Canvas width is normalized to 1.0, so the scale is just the safe-area width.
    // Callers author sizes as a fraction of the canvas and multiply by this.
    return safeArea(windowW, windowH).w;
}

namespace
{
// Read a {x,y,w,h} object into a Rect, keeping the default for any missing field.
Rect readRect(const nlohmann::json& j, const char* key, const Rect& def)
{
    const auto it = j.find(key);
    if (it == j.end() || !it->is_object())
        return def;
    return {it->value("x", def.x), it->value("y", def.y), it->value("w", def.w),
            it->value("h", def.h)};
}
} // namespace

void loadRegions(Regions& out, const std::string& path)
{
    std::ifstream f(path);
    if (!f)
        return;
    const nlohmann::json j = nlohmann::json::parse(f, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded())
        return;
    out.thought = readRect(j, "thought", out.thought);
    out.observation = readRect(j, "observation", out.observation);
    out.notification = readRect(j, "notification", out.notification);
    out.pad_x = j.value("pad_x", out.pad_x);
    out.pad_y = j.value("pad_y", out.pad_y);
    const std::string vis = j.value("visibility", std::string{"auto"});
    if (vis == "on")
        out.visibility = Visibility::On;
    else if (vis == "off")
        out.visibility = Visibility::Off;
    else
        out.visibility = Visibility::Auto;
}

Rect resolve(const Rect& canvasRect, int windowW, int windowH)
{
    return rect(canvasRect.x, canvasRect.y, canvasRect.w, canvasRect.h, windowW, windowH);
}

namespace
{
void frame(const Rect& region)
{
    // Faint backing + a 2px border, dimmer than a content panel (this is idle
    // chrome, not something asking to be read).
    UIRenderer::drawRect(region.x, region.y, region.w, region.h, {0.05f, 0.06f, 0.07f, 0.35f});
    const Color b{0.55f, 0.57f, 0.60f, 0.22f};
    constexpr float t = 2.0f;
    UIRenderer::drawRect(region.x, region.y, region.w, t, b);
    UIRenderer::drawRect(region.x, region.y + region.h - t, region.w, t, b);
    UIRenderer::drawRect(region.x, region.y, t, region.h, b);
    UIRenderer::drawRect(region.x + region.w - t, region.y, t, region.h, b);
}
} // namespace

void drawIdleFrames(const Regions& r, int windowW, int windowH)
{
    // Only the lower band is in use right now (readings are consolidated there --
    // see ThoughtBox). The upper thought region stays defined for a future re-split.
    frame(resolve(r.observation, windowW, windowH));
}

} // namespace hud
