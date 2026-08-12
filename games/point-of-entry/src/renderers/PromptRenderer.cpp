#include "renderers/PromptRenderer.h"

#include "Engine.h"
#include "UIRenderer.h"
#include "screens/ScreenStyle.h"

namespace prompt
{
namespace
{
std::string sAction;
// Offers arrive at the fixed tick rate; renders come faster. Without a grace
// window the band starves between ticks and blinks -- it holds for a few
// renders and clears only when offers actually stop.
constexpr int kGraceFrames = 8;
int sGrace = 0;
} // namespace

void offer(const std::string& action)
{
    sAction = action;
    sGrace = kGraceFrames;
}

void render(Engine& engine)
{
    if (sAction.empty() || sGrace <= 0)
        return;
    --sGrace;

    const auto w = static_cast<float>(engine.windowWidth());
    const auto h = static_cast<float>(engine.windowHeight());
    const float lh = screen_style::lineHeight();

    // A quiet band at the bottom third: wide enough to read as furniture rather than a label,
    // dark enough to sit UNDER the action instead of shouting it.
    const float bandW = w * 0.34f;
    const float bandH = lh * 1.9f;
    const float bx = (w - bandW) * 0.5f;
    const float by = h * 0.68f;
    screen_style::panel(screen_style::Rect{bx, by, bandW, bandH});

    screen_style::textInBox(sAction + "  -  space", screen_style::Rect{bx, by, bandW, bandH},
                            screen_style::kTextHot);
    if (sGrace <= 0)
        sAction.clear();
}

} // namespace prompt
