#include "Notify.h"

#include "HudCanvas.h"

#include <algorithm>
#include <vector>

namespace notify
{
namespace
{
// Item notifications: pinned to the NOTIFICATION region (the right
// side of the lower HUD band -- see config/hud.json), fading in/out in place with
// no movement. Peripheral and calm; multiple stack upward from the region top.
constexpr float kDuration = 2.6f; // total seconds on screen
constexpr float kFadeIn = 0.25f;  // fade-in window at the start
constexpr float kFadeOut = 0.6f;  // fade-out window at the end
constexpr int kMaxVisible = 6;    // cap stacked toasts (oldest dropped)
constexpr float kPadX = 10.0f;
constexpr float kPadY = 6.0f;
constexpr float kRowGap = 6.0f; // vertical gap between stacked toasts

struct Toast
{
    std::string text;
    Color color{};
    float timer = 0.0f; // counts DOWN from kDuration
};

std::vector<Toast> sToasts;
FontHandle sFont = -1;
hud::Rect sRegion; // canvas-fraction notification region (resolved per frame)
} // namespace

void init(FontHandle font, const hud::Rect& region)
{
    sFont = font;
    sRegion = region;
    sToasts.reserve(kMaxVisible);
}

void push(const std::string& text, const Color& color)
{
    sToasts.push_back(Toast{text, color, kDuration});
    while (static_cast<int>(sToasts.size()) > kMaxVisible)
        sToasts.erase(sToasts.begin());
}

void render(float dt, int window_w, int window_h)
{
    if (sToasts.empty() || sFont < 0)
        return;

    const hud::Rect r = hud::resolve(sRegion, window_w, window_h);
    const float centerX = r.x + r.w * 0.5f;
    // Newest toast at the region top; older ones stack downward from it.
    float rowTop = r.y;

    for (int i = static_cast<int>(sToasts.size()) - 1; i >= 0; --i)
    {
        Toast& t = sToasts[static_cast<std::size_t>(i)];
        t.timer -= dt;
        if (t.timer <= 0.0f)
            continue;

        // Fade in at the start, hold, fade out at the end -- no movement.
        const float age = kDuration - t.timer;
        const float alpha = std::min({1.0f, age / kFadeIn, t.timer / kFadeOut});
        Color c = t.color;
        c.a *= alpha;

        const TextSize sz = UIRenderer::measureText(sFont, t.text);
        const float x = centerX - sz.width * 0.5f; // centered in the region
        const float y = rowTop + kPadY;

        UIRenderer::drawRect(x - kPadX, y - kPadY, sz.width + kPadX * 2.0f,
                             sz.height + kPadY * 2.0f, {0.04f, 0.05f, 0.06f, 0.6f * alpha});
        UIRenderer::drawText(sFont, t.text, x, y, c);
        rowTop += sz.height + kPadY * 2.0f + kRowGap;
    }

    sToasts.erase(std::remove_if(sToasts.begin(), sToasts.end(),
                                 [](const Toast& t) { return t.timer <= 0.0f; }),
                  sToasts.end());
}

} // namespace notify
