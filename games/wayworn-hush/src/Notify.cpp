#include "Notify.h"

#include "ThoughtBox.h" // for boxTopFrac() -- toasts anchor above the HUD box

#include <algorithm>
#include <vector>

namespace notify
{
namespace
{
constexpr float kDuration = 2.4f;   // seconds on screen before it's gone
constexpr float kFadeSecs = 0.6f;   // fade-out window at the end
constexpr float kRiseSpeed = 26.0f; // px/sec the toast drifts up
constexpr int kMaxVisible = 6;      // cap stacked toasts (oldest dropped)
constexpr float kPadX = 10.0f;
constexpr float kPadY = 6.0f;
// Toasts anchor just above the HUD box and stack upward, centered like it -- so
// notifications read as coming from the same place the readings/menu appear. The
// box-top is the single source of truth (thought_box::boxTopFrac()).
constexpr float kGapAboveBox = 14.0f;

struct Toast
{
    std::string text;
    Color color{};
    float timer = 0.0f;
    float rise = 0.0f;
};

std::vector<Toast> sToasts;
FontHandle sFont = -1;
} // namespace

void init(FontHandle font)
{
    sFont = font;
    sToasts.reserve(kMaxVisible);
}

void push(const std::string& text, const Color& color)
{
    sToasts.push_back(Toast{text, color, kDuration, 0.0f});
    while (static_cast<int>(sToasts.size()) > kMaxVisible)
        sToasts.erase(sToasts.begin());
}

void render(float dt, int window_w, int window_h)
{
    if (sToasts.empty() || sFont < 0)
        return;

    const float ww = static_cast<float>(window_w);
    const float wh = static_cast<float>(window_h);
    // Bottom-most toast sits just above the box top; each older one stacks upward.
    float baselineY = wh * thought_box::boxTopFrac() - kGapAboveBox;

    for (int i = static_cast<int>(sToasts.size()) - 1; i >= 0; --i)
    {
        Toast& t = sToasts[static_cast<std::size_t>(i)];
        t.timer -= dt;
        t.rise += kRiseSpeed * dt;
        if (t.timer <= 0.0f)
            continue;

        const float alpha = std::min(1.0f, t.timer / kFadeSecs);
        Color c = t.color;
        c.a *= alpha;

        const TextSize sz = UIRenderer::measureText(sFont, t.text);
        const float x = (ww - sz.width) * 0.5f;         // centered, like the box
        const float y = baselineY - t.rise - sz.height; // stacks up above the anchor

        UIRenderer::drawRect(x - kPadX, y - kPadY, sz.width + kPadX * 2.0f,
                             sz.height + kPadY * 2.0f, {0.04f, 0.05f, 0.06f, 0.6f * alpha});
        UIRenderer::drawText(sFont, t.text, x, y, c);
        baselineY -= sz.height + kPadY * 2.0f + 6.0f;
    }

    sToasts.erase(std::remove_if(sToasts.begin(), sToasts.end(),
                                 [](const Toast& t) { return t.timer <= 0.0f; }),
                  sToasts.end());
}

} // namespace notify
