#include "ThoughtBox.h"

#include "FontManager.h"
#include "UIRenderer.h"

#include <string>

namespace thought_box
{
namespace
{
FontHandle sFont = -1;

// Per-line timing (seconds): a thought fades in, holds, fades out, then the
// next line (if any) begins. Unhurried -- silence between is part of the tone.
constexpr float kFadeIn = 0.6f;
constexpr float kHold = 2.6f;
constexpr float kFadeOut = 0.7f;
constexpr float kLineTotal = kFadeIn + kHold + kFadeOut;

std::string sLine; // line currently displaying ("" = none)
float sElapsed = 0.0f;

float alphaFor(float t)
{
    if (t < kFadeIn)
        return t / kFadeIn;
    if (t < kFadeIn + kHold)
        return 1.0f;
    return 1.0f - (t - kFadeIn - kHold) / kFadeOut;
}
} // namespace

void init(FontHandle font)
{
    sFont = font;
}

void update(observations::State& state, float dt)
{
    if (sLine.empty())
    {
        if (state.pending.empty())
            return;
        sLine = state.pending.front();
        state.pending.pop_front();
        sElapsed = 0.0f;
        return;
    }
    sElapsed += dt;
    if (sElapsed >= kLineTotal)
        sLine.clear(); // next update() pulls the following line
}

void render(int windowW, int windowH)
{
    if (sLine.empty() || sFont < 0)
        return;

    const float a = alphaFor(sElapsed);
    const auto ts = UIRenderer::measureText(sFont, sLine);

    // No panel -- just the text, centered horizontally, low in the frame. A
    // soft dark shadow keeps it legible over grass/stone without a box, per
    // the minimal UI register (docs/design/AESTHETIC.md).
    const float x = (static_cast<float>(windowW) - ts.width) * 0.5f;
    const float y = static_cast<float>(windowH) * 0.80f;

    UIRenderer::drawText(sFont, sLine, x + 2.0f, y + 2.0f, {0.0f, 0.0f, 0.0f, 0.55f * a});
    UIRenderer::drawText(sFont, sLine, x, y, {0.95f, 0.95f, 0.92f, a});
}
} // namespace thought_box
