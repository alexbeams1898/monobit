#include "NameScreen.h"

#include "HudCanvas.h"
#include "ScreenInput.h"
#include "ScreenStyle.h"
#include "UIRenderer.h"

namespace name_screen
{
namespace
{
using namespace screen_style;

text_field::State sField;
double sBlinkSecs = 0.0;

// The caret's on/off cadence. Slow -- this is a quiet game, not a terminal.
constexpr double kBlinkSecs = 0.55;

struct Layout
{
    float cx;
    float prompt_y;
    float field_x;
    float field_y;
    float field_w;
    float field_h;
    float btn_y;
    float btn_w;
    float btn_h;
    float set_out_x;
    float back_x;
};

Layout layout(int windowW, int windowH)
{
    const float s = hud::scale(windowW, windowH);
    const float wh = static_cast<float>(windowH);
    Layout lo;
    lo.cx = static_cast<float>(windowW) * 0.5f;
    lo.prompt_y = wh * 0.34f;
    lo.field_w = s * 0.30f;
    lo.field_h = lineH(bodyFont()) * 1.5f;
    lo.field_x = lo.cx - lo.field_w * 0.5f;
    lo.field_y = wh * 0.44f;
    lo.btn_w = s * 0.09f;
    lo.btn_h = lineH(bodyFont()) * 1.4f;
    lo.btn_y = lo.field_y + lo.field_h + lineH(bodyFont()) * 1.1f;
    lo.back_x = lo.cx - lo.btn_w - s * 0.01f;
    lo.set_out_x = lo.cx + s * 0.01f;
    return lo;
}
} // namespace

void reset()
{
    text_field::begin(sField);
    sBlinkSecs = 0.0;
}

const std::string& name()
{
    return sField.text;
}

Action step(const Keys& keys, double dt)
{
    text_field::Input in;
    in.typed = keys.typed;
    in.backspace = keys.backspace;
    in.del = keys.del;
    in.left = keys.left;
    in.right = keys.right;
    in.home = keys.home;
    in.end = keys.end;
    text_field::update(sField, in, dt);

    sBlinkSecs += dt;

    if (keys.back)
        return Action::Back;
    // A blank name is refused rather than accepted-and-fixed-up: the pilgrim is
    // being asked who they are, and "nobody" isn't an answer to hand back.
    if (keys.confirm && text_field::acceptable(sField.text))
        return Action::Confirm;
    return Action::None;
}

Action render(const Mouse& mouse, int windowW, int windowH)
{
    const float ww = static_cast<float>(windowW);
    const float wh = static_cast<float>(windowH);
    const Layout lo = layout(windowW, windowH);

    UIRenderer::drawRect(0.0f, 0.0f, ww, wh, kOverlay);
    softTextCentered("Who sets out?", lo.cx, lo.prompt_y, kText);

    // The field: a soft trough rather than a boxed input -- the register has no
    // hard chrome anywhere else.
    UIRenderer::drawRect(lo.field_x, lo.field_y, lo.field_w, lo.field_h, kRowActive);

    const float textX = lo.field_x + lo.field_h * 0.4f;
    const float textY = lo.field_y + (lo.field_h - lineH(bodyFont()) * 0.72f) * 0.5f;
    softText(sField.text, textX, textY, kText);

    // The caret sits at the cursor, measured through the same text the field
    // holds so it can't drift from what's drawn.
    const bool caretOn = static_cast<int>(sBlinkSecs / kBlinkSecs) % 2 == 0;
    if (caretOn)
    {
        const std::string upToCursor =
            sField.text.substr(0, static_cast<std::size_t>(sField.cursor));
        const float caretX = textX + UIRenderer::measureText(bodyFont(), upToCursor).width;
        UIRenderer::drawRect(caretX, textY, 2.0f, lineH(bodyFont()) * 0.72f, kText);
    }

    // Set out is disabled until there's a name -- dim and inert, but still there, so it
    // doesn't move under the hand the moment it becomes pressable.
    const bool ok = text_field::acceptable(sField.text);
    const bool backHot = buttonHit(mouse.x, mouse.y, lo.back_x, lo.btn_y, lo.btn_w, lo.btn_h);
    const bool setHot =
        ok && buttonHit(mouse.x, mouse.y, lo.set_out_x, lo.btn_y, lo.btn_w, lo.btn_h);

    button("Back", lo.back_x, lo.btn_y, lo.btn_w, lo.btn_h, backHot);
    button("Set out", lo.set_out_x, lo.btn_y, lo.btn_w, lo.btn_h, setHot, /*enabled=*/ok);

    if (mouse.clicked && setHot)
        return Action::Confirm;
    if (mouse.clicked && backHot)
        return Action::Back;
    return Action::None;
}

} // namespace name_screen
