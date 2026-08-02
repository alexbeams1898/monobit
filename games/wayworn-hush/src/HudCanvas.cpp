#include "HudCanvas.h"

#include "JsonConfig.h"
#include "Settings.h"
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
    out.upper = readRect(j, "upper", out.upper);
    out.content = readRect(j, "content", out.content);
    out.notification = readRect(j, "notification", out.notification);
    out.pad_x = j.value("pad_x", out.pad_x);
    out.pad_y = j.value("pad_y", out.pad_y);
}

Visibility loadVisibility(const std::string& path, Visibility fallback)
{
    // The authored DEFAULT. A player's saved preference overrides this at load (see
    // settings::Settings) -- config is where the game ships, not where the player's choice
    // lives. The name<->value mapping is settings' so config and the save can't disagree
    // about what "auto" means.
    const auto loaded = config::load(path);
    if (!loaded)
        return fallback;
    const std::string vis = loaded->value("visibility", std::string{});
    return settings::visibilityFromName(vis.c_str(), fallback);
}

Rect resolve(const Rect& canvasRect, int windowW, int windowH)
{
    return rect(canvasRect.x, canvasRect.y, canvasRect.w, canvasRect.h, windowW, windowH);
}

} // namespace hud
