#include "Tutorial.h"

#include "JsonConfig.h"
#include "ScreenStyle.h"
#include "UIRenderer.h"

#include <nlohmann/json.hpp>

namespace tutorial
{

void load(Config& cfg, const std::string& path)
{
    cfg.cards.clear();
    const auto loaded = config::load(path);
    if (!loaded)
        return;
    for (const auto& c : loaded->value("cards", nlohmann::json::array()))
    {
        Card card;
        card.id = c.value("id", std::string{});
        card.on = c.value("on", std::string{});
        card.title = c.value("title", std::string{});
        card.body = c.value("body", std::string{});
        card.focus = c.value("focus", std::string{});
        if (!card.id.empty() && !card.on.empty())
            cfg.cards.push_back(std::move(card));
    }
}

void fire(State& st, const Config& cfg, const std::string& event)
{
    for (const auto& c : cfg.cards)
        if (c.on == event && st.seen.insert(c.id).second)
            st.queue.push_back(&c);
}

const Card* current(const State& st)
{
    return st.queue.empty() ? nullptr : st.queue.front();
}

void dismiss(State& st)
{
    if (!st.queue.empty())
        st.queue.pop_front();
}

// --- render ----------------------------------------------------------------

namespace
{
FontHandle sFont = -1;
FontHandle sHeadingFont = -1;

// The hold is dimmer than a menu overlay but not opaque: the lit hole is the
// point, and the rest of the world stays readable as context behind it.
constexpr Color kHold{0.03f, 0.04f, 0.05f, 0.78f};

// Dim everything EXCEPT `hole` (four rects around it), and edge the hole so the
// eye lands on it.
void dimAround(const hud::Rect& hole, float w, float h)
{
    UIRenderer::drawRect(0.0f, 0.0f, w, hole.y, kHold);
    UIRenderer::drawRect(0.0f, hole.y + hole.h, w, h - (hole.y + hole.h), kHold);
    UIRenderer::drawRect(0.0f, hole.y, hole.x, hole.h, kHold);
    UIRenderer::drawRect(hole.x + hole.w, hole.y, w - (hole.x + hole.w), hole.h, kHold);
    screen_style::border(hole.x, hole.y, hole.w, hole.h, {0.95f, 0.95f, 0.92f, 0.35f});
}
} // namespace

void init(FontHandle body_font, FontHandle heading_font)
{
    sFont = body_font;
    sHeadingFont = heading_font;
}

void render(const Card& card, const hud::Regions& regions, const FocusRect& spirit, int windowW,
            int windowH)
{
    const float w = static_cast<float>(windowW);
    const float h = static_cast<float>(windowH);

    // Hone in: the named region stays lit, the rest holds its breath. All
    // content boxes live in the lower content band; toasts on the right. No
    // focus (or an unknown name) dims the whole frame.
    const hud::Rect* focus = nullptr;
    if (card.focus == "content")
        focus = &regions.content;
    else if (card.focus == "notification")
        focus = &regions.notification;
    hud::Rect hole{};
    if (card.focus == "spirit" && spirit.w > 0.0f)
    {
        // Already in window px -- the badge resolved its own box, so nothing re-derives it.
        hole = hud::Rect{spirit.x, spirit.y, spirit.w, spirit.h};
        dimAround(hole, w, h);
    }
    else if (focus != nullptr)
    {
        hole = hud::resolve(*focus, windowW, windowH);
        dimAround(hole, w, h);
    }
    else
    {
        UIRenderer::drawRect(0.0f, 0.0f, w, h, kHold);
    }

    // The card parks in the UPPER band -- no content ever renders there, so it
    // can never cover the thing it points at.
    const hud::Rect band = hud::resolve(regions.upper, windowW, windowH);
    const float padX = regions.pad_x * hud::scale(windowW, windowH);
    const float padY = regions.pad_y * hud::scale(windowW, windowH);

    const float headingH = screen_style::lineH(sHeadingFont);
    const float bodyH = screen_style::lineH(sFont);
    const float innerW = band.w - 2.0f * padX;
    const std::vector<std::string> lines = screen_style::wrapText(sFont, card.body, innerW);
    const float cardH = 2.0f * padY + headingH + bodyH * static_cast<float>(lines.size()) + bodyH;

    UIRenderer::drawRect(band.x, band.y, band.w, cardH, {0.05f, 0.06f, 0.07f, 0.94f});
    screen_style::border(band.x, band.y, band.w, cardH, {0.95f, 0.95f, 0.92f, 0.55f});

    float y = band.y + padY;
    UIRenderer::drawText(sHeadingFont, card.title, band.x + padX, y, screen_style::kText);
    y += headingH;
    for (const auto& line : lines)
    {
        UIRenderer::drawText(sFont, line, band.x + padX, y, screen_style::kText);
        y += bodyH;
    }
    const float hintW = UIRenderer::measureText(sHeadingFont, "Space").width;
    UIRenderer::drawText(sHeadingFont, "Space", band.x + band.w - padX - hintW, y,
                         screen_style::kTextDim);
}

} // namespace tutorial
