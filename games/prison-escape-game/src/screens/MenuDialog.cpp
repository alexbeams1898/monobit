#include "screens/MenuDialog.h"

#include "ecs/EntityManager.h"
#include "screens/ScreenColors.h"
#include "screens/ScreenInput.h"

#include <SDL.h>
#include <algorithm>
#include <cmath>

using screen_input::keyPressed;
using screen_input::mouseClicked;
using namespace screen_colors;

static constexpr Color TITLE_COLOR{0.6f, 0.85f, 0.7f, 1.0f};
static constexpr Color DISABLED_COLOR{0.35f, 0.33f, 0.32f, 0.7f};
static constexpr Color SELECTED_BG{0.2f, 0.3f, 0.25f, 0.6f};
static constexpr Color SEPARATOR{0.3f, 0.4f, 0.35f, 0.5f};
static constexpr Color HINT_COLOR{0.5f, 0.48f, 0.46f, 0.8f};

namespace
{

struct MenuInputResult
{
    bool dismissed = false;
    bool activate = false;
};

static MenuInputResult handleMenuInput(EntityManager& em, const MenuDialog::Options& opts, int& sel,
                                       int itemCount)
{
    MenuInputResult res;

    if (opts.close_on_escape && keyPressed(em, SDL_SCANCODE_ESCAPE))
    {
        res.dismissed = true;
        return res;
    }
    if (opts.close_on_rmb && mouseClicked(em, SDL_BUTTON_RIGHT))
    {
        res.dismissed = true;
        return res;
    }

    if (keyPressed(em, SDL_SCANCODE_UP) || keyPressed(em, SDL_SCANCODE_W))
    {
        if (sel <= 0)
            sel = itemCount - 1;
        else
            sel--;
    }
    if (keyPressed(em, SDL_SCANCODE_DOWN) || keyPressed(em, SDL_SCANCODE_S))
    {
        if (sel < 0 || sel >= itemCount - 1)
            sel = 0;
        else
            sel++;
    }

    if (sel >= 0 && (keyPressed(em, SDL_SCANCODE_RETURN) || keyPressed(em, SDL_SCANCODE_KP_ENTER)))
        res.activate = true;

    return res;
}

struct MenuLayout
{
    float panel_w, panel_h, content_w, max_label_w, desc_gap;
    float px, py, cx, cw;
    float title_h, line_h, sep_gap;
    TextSize tsz, hsz;
};

static MenuLayout measureMenuContent(const MenuDialog::Options& opts, int itemCount, float window_w,
                                     float window_h)
{
    MenuLayout l{};
    const float pad = opts.padding;
    l.title_h = FontManager::lineHeight(opts.title_font);
    l.line_h = FontManager::lineHeight(opts.body_font) + 8.0f;
    l.desc_gap = 16.0f; // space between label column and description column

    // Compute column widths.
    l.max_label_w = 0.0f;
    float max_desc_w = 0.0f;
    for (const auto& item : opts.items)
    {
        const TextSize lsz = UIRenderer::measureText(opts.body_font, "> " + item.label);
        l.max_label_w = std::max(l.max_label_w, lsz.width);
        if (!item.description.empty())
        {
            const TextSize dsz = UIRenderer::measureText(opts.body_font, item.description);
            max_desc_w = std::max(max_desc_w, dsz.width);
        }
    }

    l.content_w = l.max_label_w;
    if (max_desc_w > 0.0f)
        l.content_w += l.desc_gap + max_desc_w;

    // Title width.
    l.tsz = UIRenderer::measureText(opts.title_font, opts.title);
    l.content_w = std::max(l.content_w, l.tsz.width);

    // Hint width.
    l.hsz = {};
    if (!opts.hint.empty())
        l.hsz = UIRenderer::measureText(opts.body_font, opts.hint);
    l.content_w = std::max(l.content_w, l.hsz.width);

    l.content_w = std::max(l.content_w, opts.min_width - pad * 2.0f);
    l.panel_w = l.content_w + pad * 2.0f;

    // Heights: padding + title + sep + items + sep + hint + padding.
    l.sep_gap = 14.0f; // space around separator lines
    const float hint_block =
        opts.hint.empty() ? 0.0f : (l.sep_gap + 1.0f + l.sep_gap + l.hsz.height);
    l.panel_h = pad + l.title_h + l.sep_gap + 1.0f + l.sep_gap +
                static_cast<float>(itemCount) * l.line_h + hint_block + pad;

    l.px = (window_w - l.panel_w) * 0.5f;
    l.py = (window_h - l.panel_h) * 0.5f;
    l.cx = l.px + pad;
    l.cw = l.content_w;

    return l;
}

static bool drawMenuItems(const MenuDialog::Options& opts, int& sel, bool activate,
                          const MenuLayout& l, float y, float mx, float my, EntityManager& em)
{
    const float desc_x = l.cx + l.max_label_w + l.desc_gap;
    const int itemCount = static_cast<int>(opts.items.size());

    for (int i = 0; i < itemCount; ++i)
    {
        const bool hovered = mx >= l.cx - 4.0f && mx < l.cx + l.cw + 4.0f && my >= y - 2.0f &&
                             my < y - 2.0f + l.line_h;

        if (hovered && opts.items[i].enabled && mouseClicked(em, SDL_BUTTON_LEFT))
        {
            sel = i;
            activate = true;
        }

        const bool selected = (i == sel);
        if (selected)
            UIRenderer::drawRect(l.cx - 4.0f, y - 2.0f, l.cw + 8.0f, l.line_h, SELECTED_BG);
        else if (hovered)
            UIRenderer::drawRect(l.cx - 4.0f, y - 2.0f, l.cw + 8.0f, l.line_h, HOVERED_BG);

        const bool highlighted = selected || hovered;
        const std::string prefix = selected ? "> " : "  ";
        const Color& labelColor = opts.items[i].enabled ? TEXT_WHITE : DISABLED_COLOR;
        UIRenderer::drawText(opts.body_font, prefix + opts.items[i].label, l.cx, y,
                             highlighted ? labelColor : TEXT_DIM);

        if (!opts.items[i].description.empty())
        {
            UIRenderer::drawText(opts.body_font, opts.items[i].description, desc_x, y, TEXT_DIM);
        }

        y += l.line_h;
    }

    return activate;
}

} // anonymous namespace

MenuDialog::Result MenuDialog::render(EntityManager& em, const Options& opts, float window_w,
                                      float window_h)
{
    Result result;
    const int itemCount = static_cast<int>(opts.items.size());
    if (itemCount == 0)
        return result;

    int& sel = *opts.selection;
    if (sel >= 0)
        sel = std::clamp(sel, 0, itemCount - 1);

    auto input = handleMenuInput(em, opts, sel, itemCount);
    if (input.dismissed)
    {
        result.dismissed = true;
        return result;
    }

    const MenuLayout l = measureMenuContent(opts, itemCount, window_w, window_h);

    int mouseX = 0;
    int mouseY = 0;
    SDL_GetMouseState(&mouseX, &mouseY);
    const float mx = static_cast<float>(mouseX);
    const float my = static_cast<float>(mouseY);

    if (opts.darken_background)
        UIRenderer::drawRect(0.0f, 0.0f, window_w, window_h, OVERLAY);
    UIRenderer::drawRect(l.px, l.py, l.panel_w, l.panel_h, PANEL_BG);

    float y = l.py + opts.padding;
    UIRenderer::drawText(opts.title_font, opts.title, l.px + (l.panel_w - l.tsz.width) * 0.5f, y,
                         TITLE_COLOR);
    y += l.title_h + l.sep_gap;

    UIRenderer::drawRect(l.cx, y, l.cw, 1.0f, SEPARATOR);
    y += 1.0f + l.sep_gap;

    const bool activate = drawMenuItems(opts, sel, input.activate, l, y, mx, my, em);
    y += static_cast<float>(itemCount) * l.line_h;

    if (!opts.hint.empty())
    {
        y += l.sep_gap;
        UIRenderer::drawRect(l.cx, y, l.cw, 1.0f, SEPARATOR);
        y += 1.0f + l.sep_gap;
        UIRenderer::drawText(opts.body_font, opts.hint, l.px + (l.panel_w - l.hsz.width) * 0.5f, y,
                             HINT_COLOR);
    }

    if (activate && sel >= 0 && sel < itemCount && opts.items[sel].enabled)
        result.selected = sel;

    return result;
}
