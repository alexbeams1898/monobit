#include "screens/MenuDialog.h"

#include "ecs/EntityManager.h"

#include <SDL.h>
#include <algorithm>
#include <cmath>

static constexpr Color OVERLAY{0.0f, 0.0f, 0.0f, 0.75f};
static constexpr Color PANEL_BG{0.06f, 0.06f, 0.09f, 0.95f};
static constexpr Color TITLE_COLOR{0.6f, 0.85f, 0.7f, 1.0f};
static constexpr Color TEXT_WHITE{0.92f, 0.90f, 0.88f, 1.0f};
static constexpr Color TEXT_DIM{0.5f, 0.48f, 0.46f, 1.0f};
static constexpr Color DISABLED_COLOR{0.35f, 0.33f, 0.32f, 0.7f};
static constexpr Color SELECTED_BG{0.2f, 0.3f, 0.25f, 0.6f};
static constexpr Color SEPARATOR{0.3f, 0.4f, 0.35f, 0.5f};
static constexpr Color HINT_COLOR{0.5f, 0.48f, 0.46f, 0.8f};

static bool keyPressed(const EntityManager& em, int scancode)
{
    const auto& kd = em.key_down_events;
    return std::find(kd.begin(), kd.end(), scancode) != kd.end();
}

static bool mouseClicked(const EntityManager& em, uint8_t button)
{
    for (uint8_t btn : em.mouse_down_events)
        if (btn == button)
            return true;
    return false;
}

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

    // --- Input ---
    if (opts.close_on_escape && keyPressed(em, SDL_SCANCODE_ESCAPE))
    {
        result.dismissed = true;
        return result;
    }
    if (opts.close_on_rmb && mouseClicked(em, SDL_BUTTON_RIGHT))
    {
        result.dismissed = true;
        return result;
    }

    // Find next/prev enabled item, wrapping around. Returns -1 if none enabled.
    auto findEnabled = [&](int from, int dir) -> int {
        for (int i = 0; i < itemCount; ++i)
        {
            int idx = ((from + dir * (i + 1)) % itemCount + itemCount) % itemCount;
            if (opts.items[idx].enabled)
                return idx;
        }
        return -1;
    };

    if (keyPressed(em, SDL_SCANCODE_UP) || keyPressed(em, SDL_SCANCODE_W))
    {
        int start = (sel >= 0) ? sel : 0;
        sel = findEnabled(start, -1);
    }
    if (keyPressed(em, SDL_SCANCODE_DOWN) || keyPressed(em, SDL_SCANCODE_S))
    {
        int start = (sel >= 0) ? sel : itemCount - 1;
        sel = findEnabled(start, 1);
    }

    bool activate = false;
    if (sel >= 0 && (keyPressed(em, SDL_SCANCODE_RETURN) || keyPressed(em, SDL_SCANCODE_KP_ENTER)))
        activate = true;

    // --- Measure content ---
    const float pad = opts.padding;
    const float title_h = FontManager::lineHeight(opts.title_font);
    const float line_h = FontManager::lineHeight(opts.body_font) + 8.0f;
    const float desc_gap = 16.0f; // space between label column and description column

    // Compute column widths.
    float max_label_w = 0.0f;
    float max_desc_w = 0.0f;
    for (const auto& item : opts.items)
    {
        TextSize lsz = UIRenderer::measureText(opts.body_font, "> " + item.label);
        max_label_w = std::max(max_label_w, lsz.width);
        if (!item.description.empty())
        {
            TextSize dsz = UIRenderer::measureText(opts.body_font, item.description);
            max_desc_w = std::max(max_desc_w, dsz.width);
        }
    }

    float content_w = max_label_w;
    if (max_desc_w > 0.0f)
        content_w += desc_gap + max_desc_w;

    // Title width.
    TextSize tsz = UIRenderer::measureText(opts.title_font, opts.title);
    content_w = std::max(content_w, tsz.width);

    // Hint width.
    TextSize hsz{};
    if (!opts.hint.empty())
        hsz = UIRenderer::measureText(opts.body_font, opts.hint);
    content_w = std::max(content_w, hsz.width);

    content_w = std::max(content_w, opts.min_width - pad * 2.0f);
    const float panel_w = content_w + pad * 2.0f;

    // Heights: padding + title + sep + items + sep + hint + padding.
    const float sep_gap = 14.0f;     // space around separator lines
    const float hint_block = opts.hint.empty() ? 0.0f : (sep_gap + 1.0f + sep_gap + hsz.height);
    const float panel_h = pad + title_h + sep_gap + 1.0f + sep_gap +
                           static_cast<float>(itemCount) * line_h + hint_block + pad;

    const float px = (window_w - panel_w) * 0.5f;
    const float py = (window_h - panel_h) * 0.5f;
    const float cx = px + pad;
    const float cw = content_w;

    int mouseX = 0;
    int mouseY = 0;
    SDL_GetMouseState(&mouseX, &mouseY);
    const float mx = static_cast<float>(mouseX);
    const float my = static_cast<float>(mouseY);

    // --- Draw ---
    if (opts.darken_background)
        UIRenderer::drawRect(0.0f, 0.0f, window_w, window_h, OVERLAY);
    UIRenderer::drawRect(px, py, panel_w, panel_h, PANEL_BG);

    float y = py + pad;

    // Title (centered).
    UIRenderer::drawText(opts.title_font, opts.title, px + (panel_w - tsz.width) * 0.5f, y,
                         TITLE_COLOR);
    y += title_h + sep_gap;

    // Separator.
    UIRenderer::drawRect(cx, y, cw, 1.0f, SEPARATOR);
    y += 1.0f + sep_gap;

    // Description column x-offset.
    const float desc_x = cx + max_label_w + desc_gap;

    // Option rows.
    for (int i = 0; i < itemCount; ++i)
    {
        const bool hovered = (mx >= cx - 4.0f && mx < cx + cw + 4.0f && my >= y - 2.0f &&
                              my < y - 2.0f + line_h);
        if (hovered && opts.items[i].enabled)
            sel = i;

        const bool selected = (i == sel);
        if (selected)
            UIRenderer::drawRect(cx - 4.0f, y - 2.0f, cw + 8.0f, line_h, SELECTED_BG);

        const std::string prefix = selected ? "> " : "  ";
        const Color& labelColor = opts.items[i].enabled ? TEXT_WHITE : DISABLED_COLOR;
        UIRenderer::drawText(opts.body_font, prefix + opts.items[i].label, cx, y,
                             selected ? labelColor : TEXT_DIM);

        if (!opts.items[i].description.empty())
        {
            UIRenderer::drawText(opts.body_font, opts.items[i].description, desc_x, y, TEXT_DIM);
        }

        if (hovered && mouseClicked(em, SDL_BUTTON_LEFT))
            activate = true;

        y += line_h;
    }

    // Hint.
    if (!opts.hint.empty())
    {
        y += sep_gap;
        UIRenderer::drawRect(cx, y, cw, 1.0f, SEPARATOR);
        y += 1.0f + sep_gap;
        UIRenderer::drawText(opts.body_font, opts.hint, px + (panel_w - hsz.width) * 0.5f, y,
                             HINT_COLOR);
    }

    // Activate.
    if (activate && sel >= 0 && sel < itemCount && opts.items[sel].enabled)
        result.selected = sel;

    return result;
}
