#include "LoadScreen.h"

#include "HudCanvas.h"
#include "ScreenStyle.h"
#include "UIRenderer.h"

#include <algorithm>

namespace load_screen
{
namespace
{
using namespace screen_style;

int sSel = 0;
std::string sId;
// The pilgrim a Forget is waiting on. Empty = nothing pending. Forgetting a walk is the
// one irreversible thing on this screen, so it asks first -- and asks about someone in
// particular, by id, so the answer can't land on whoever the cursor moved to since.
std::string sPendingForget;

// What an unnamed pilgrim reads as. They are still someone -- they just never said.
constexpr const char* kUnnamed = "(unnamed)";

struct Layout
{
    float cx;
    float title_y;
    float first_y;
    float row_h;   // the row's full height
    float row_gap; // the breath between rows
    float row_w;
    float row_x;
    float x_size; // the forget control: a small square at the row's right edge
};

Layout layout(int windowW, int windowH)
{
    const float s = hud::scale(windowW, windowH);
    const float wh = static_cast<float>(windowH);
    const float line = lineH(bodyFont());
    Layout lo;
    lo.cx = static_cast<float>(windowW) * 0.5f;
    lo.title_y = wh * 0.22f;
    lo.first_y = wh * 0.34f;
    // Two lines of text (name, then how far they got) plus padding above and below --
    // derived from the line height so the text can never outgrow the row it sits in.
    lo.row_h = line * 2.0f + line * 0.7f;
    lo.row_gap = line * 0.4f;
    lo.row_w = s * 0.44f;
    lo.row_x = lo.cx - lo.row_w * 0.5f;
    lo.x_size = line * 1.2f;
    return lo;
}

// A row's top edge. Rows are spaced by their height plus the gap.
float rowY(const Layout& lo, int i)
{
    return lo.first_y + static_cast<float>(i) * (lo.row_h + lo.row_gap);
}

// The forget control: a small square inset from the row's right edge, vertically centered.
float xX(const Layout& lo)
{
    return lo.row_x + lo.row_w - lo.x_size - lineH(bodyFont()) * 0.5f;
}
float xY(const Layout& lo, int i)
{
    return rowY(lo, i) + (lo.row_h - lo.x_size) * 0.5f;
}

void clamp(int count)
{
    sSel = count > 0 ? std::clamp(sSel, 0, count - 1) : 0;
}

// One pilgrim's row: the whole bar is the way back to their walk -- clicking anywhere on it
// sets out as them. The only other thing on the row is a small x that asks to forget them.
// Returns what a click committed (an x only ASKS -- it never forgets here).
Action renderRow(const Entry& e, const Mouse& mouse, const Layout& lo, int i)
{
    const float y = rowY(lo, i);
    const float xx = xX(lo);
    const float xy = xY(lo, i);

    const bool rowHot = buttonHit(mouse.x, mouse.y, lo.row_x, y, lo.row_w, lo.row_h);
    const bool xHot = buttonHit(mouse.x, mouse.y, xx, xy, lo.x_size, lo.x_size);
    if (rowHot)
        sSel = i; // the hand and the keyboard cursor agree about who is being talked about
    const bool active = i == sSel;

    // The bar itself is the button: it lifts under the hand, except where the x sits (so
    // reaching for the x doesn't look like you're about to set out).
    UIRenderer::drawRect(lo.row_x, y, lo.row_w, lo.row_h,
                         (active && !xHot) ? kRowActive : kButtonRest);

    const float alpha = active ? 1.0f : kIdleAlpha;
    const float textX = lo.row_x + lineH(bodyFont()) * 0.6f;
    const float textY = y + lineH(bodyFont()) * 0.35f;
    softText(e.name.empty() ? kUnnamed : e.name, textX, textY, kText, alpha);

    // How far they got. A pilgrim who never set out says so rather than showing a day that
    // would read as progress.
    const std::string far =
        e.walked ? ("day " + std::to_string(std::max(1, e.day))) : std::string("not yet set out");
    softText(far, textX, textY + lineH(bodyFont()), kTextDim, alpha);

    // The forget control: quiet until reached for, then it warms.
    UIRenderer::drawRect(xx, xy, lo.x_size, lo.x_size, xHot ? kRowActive : kButtonOff);
    const auto ts = UIRenderer::measureText(bodyFont(), "x");
    softText("x", xx + (lo.x_size - ts.width) * 0.5f, xy + (lo.x_size - ts.height) * 0.5f - 2.0f,
             kText, xHot ? 1.0f : kIdleAlpha);

    if (!mouse.clicked)
        return Action::None;
    if (xHot)
    {
        sPendingForget = e.id; // ask first -- the confirmation does the forgetting
        return Action::None;
    }
    if (rowHot)
    {
        sId = e.id;
        return Action::Walk;
    }
    return Action::None;
}

// The confirmation, drawn over the roster. Returns the action it committed.
Action renderConfirm(const std::vector<Entry>& entries, const Mouse& mouse, int windowW,
                     int windowH)
{
    const float ww = static_cast<float>(windowW);
    const float wh = static_cast<float>(windowH);
    const float s = hud::scale(windowW, windowH);
    const float cx = ww * 0.5f;

    // Whose walk is at stake -- named, so the answer is about a person, not a row.
    std::string who = kUnnamed;
    for (const auto& e : entries)
        if (e.id == sPendingForget && !e.name.empty())
            who = e.name;

    UIRenderer::drawRect(0.0f, 0.0f, ww, wh, kOverlay);
    softTextCentered("Forget " + who + "?", cx, wh * 0.42f, kText);
    softTextCentered("Their walk cannot be recovered.", cx, wh * 0.42f + lineH(bodyFont()) * 1.4f,
                     kTextDim);

    const float bw = s * 0.09f;
    const float bh = lineH(bodyFont()) * 1.4f;
    const float by = wh * 0.54f;
    const float keepX = cx - bw - s * 0.01f;
    const float forgetX2 = cx + s * 0.01f;

    // Keep is the near one and reads brighter: the safe answer shouldn't need aim.
    const bool keepHot = buttonHit(mouse.x, mouse.y, keepX, by, bw, bh);
    const bool forgetHot = buttonHit(mouse.x, mouse.y, forgetX2, by, bw, bh);
    button("Keep", keepX, by, bw, bh, keepHot);
    button("Forget", forgetX2, by, bw, bh, forgetHot);

    if (mouse.clicked && forgetHot)
    {
        sId = sPendingForget;
        sPendingForget.clear();
        return Action::Forget;
    }
    if (mouse.clicked && keepHot)
        sPendingForget.clear();
    return Action::None;
}
} // namespace

void reset()
{
    sSel = 0;
    sId.clear();
    sPendingForget.clear();
}

const std::string& id()
{
    return sId;
}

Action step(const std::vector<Entry>& entries, bool up, bool down, bool confirm, bool forget,
            bool back)
{
    const int count = static_cast<int>(entries.size());
    clamp(count);

    // While a confirmation is up it owns the keys: back dismisses it, confirm answers it.
    // Otherwise a stray Enter meant for the roster would forget somebody.
    if (!sPendingForget.empty())
    {
        if (back)
            sPendingForget.clear();
        else if (confirm)
        {
            sId = sPendingForget;
            sPendingForget.clear();
            return Action::Forget;
        }
        return Action::None;
    }

    if (back)
        return Action::Back;
    if (count == 0)
        return Action::None; // nothing to move over or choose

    if (down)
        sSel = (sSel + 1) % count;
    else if (up)
        sSel = (sSel - 1 + count) % count;

    if (confirm)
    {
        sId = entries[static_cast<std::size_t>(sSel)].id;
        return Action::Walk;
    }
    if (forget)
        sPendingForget = entries[static_cast<std::size_t>(sSel)].id; // ask, don't do
    return Action::None;
}

Action render(const std::vector<Entry>& entries, const Mouse& mouse, int windowW, int windowH)
{
    const float ww = static_cast<float>(windowW);
    const float wh = static_cast<float>(windowH);
    const Layout lo = layout(windowW, windowH);
    const int count = static_cast<int>(entries.size());
    clamp(count);

    if (!sPendingForget.empty())
        return renderConfirm(entries, mouse, windowW, windowH);

    UIRenderer::drawRect(0.0f, 0.0f, ww, wh, kOverlay);
    softTextCentered("Who have you been?", lo.cx, lo.title_y, kText);

    const float backW = hud::scale(windowW, windowH) * 0.09f;
    const float backH = lineH(bodyFont()) * 1.4f;
    const float backY = wh * 0.82f;
    const float backX = lo.cx - backW * 0.5f;

    if (count == 0)
    {
        softTextCentered("No one yet.", lo.cx, lo.first_y, kTextDim);
        const bool hot = buttonHit(mouse.x, mouse.y, backX, backY, backW, backH);
        button("Back", backX, backY, backW, backH, hot);
        return (mouse.clicked && hot) ? Action::Back : Action::None;
    }

    Action committed = Action::None;
    for (int i = 0; i < count; ++i)
    {
        const Action a = renderRow(entries[static_cast<std::size_t>(i)], mouse, lo, i);
        if (a != Action::None)
            committed = a;
    }

    const bool backHot = buttonHit(mouse.x, mouse.y, backX, backY, backW, backH);
    button("Back", backX, backY, backW, backH, backHot);
    if (mouse.clicked && backHot)
        committed = Action::Back;

    return committed;
}

} // namespace load_screen
