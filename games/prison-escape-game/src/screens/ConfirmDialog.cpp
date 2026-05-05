#include "screens/ConfirmDialog.h"

#include "ecs/EntityManager.h"
#include "screens/ScreenColors.h"
#include "screens/ScreenInput.h"
#include "systems/AudioSystem.h"

#include <SDL.h>
#include <algorithm>

using screen_input::keyPressed;
using screen_input::mouseClicked;
using namespace screen_colors;

static constexpr Color DIALOG_BG{0.1f, 0.08f, 0.12f, 0.95f};
static constexpr Color TITLE_COLOR{0.95f, 0.3f, 0.25f, 1.0f};
static constexpr Color BODY_COLOR{0.5f, 0.48f, 0.46f, 1.0f};
static constexpr Color YES_BG{0.4f, 0.08f, 0.08f, 0.6f};
static constexpr Color YES_BG_HL{0.6f, 0.12f, 0.12f, 0.8f};

ConfirmDialog::Result ConfirmDialog::render(EntityManager& em, const Options& opts, float window_w,
                                            float window_h)
{
    Result result = Result::None;
    int& sel = *opts.selection;

    // --- Input ---
    if (keyPressed(em, SDL_SCANCODE_ESCAPE) || keyPressed(em, SDL_SCANCODE_N))
        return Result::No;
    if (keyPressed(em, SDL_SCANCODE_Y))
        return Result::Yes;

    if (keyPressed(em, SDL_SCANCODE_LEFT) || keyPressed(em, SDL_SCANCODE_A))
        sel = 0;
    if (keyPressed(em, SDL_SCANCODE_RIGHT) || keyPressed(em, SDL_SCANCODE_D))
        sel = 1;

    if (keyPressed(em, SDL_SCANCODE_RETURN) || keyPressed(em, SDL_SCANCODE_KP_ENTER))
        return (sel == 0) ? Result::Yes : Result::No;

    // --- Measure content to compute panel size ---
    const float pad = opts.padding;
    const float title_h = FontManager::lineHeight(opts.title_font);
    const float body_line_h = FontManager::lineHeight(opts.body_font) + 2.0f;
    const float btn_h = 36.0f;
    const float btn_w = 80.0f;
    const float btn_gap = 24.0f;
    const float section_gap = 12.0f;

    // Width: max of title, all body lines, button row, and min_width.
    float content_w = opts.min_width;
    const TextSize tsz = UIRenderer::measureText(opts.title_font, opts.title);
    content_w = std::max(content_w, tsz.width);
    for (const auto& line : opts.body_lines)
    {
        const TextSize lsz = UIRenderer::measureText(opts.body_font, line);
        content_w = std::max(content_w, lsz.width);
    }
    const float dlg_w = content_w + pad * 2.0f;

    // Height: title + gap + body lines + gap + buttons + padding top/bottom.
    float body_h = 0.0f;
    if (!opts.body_lines.empty())
        body_h = static_cast<float>(opts.body_lines.size()) * body_line_h + section_gap;
    const float dlg_h = pad + title_h + section_gap + body_h + btn_h + pad;

    const float dx = (window_w - dlg_w) * 0.5f;
    const float dy = (window_h - dlg_h) * 0.5f;

    int mouseX = 0;
    int mouseY = 0;
    SDL_GetMouseState(&mouseX, &mouseY);
    const float mx = static_cast<float>(mouseX);
    const float my = static_cast<float>(mouseY);

    // --- Draw ---
    UIRenderer::drawRect(0.0f, 0.0f, window_w, window_h, OVERLAY);
    UIRenderer::drawRect(dx, dy, dlg_w, dlg_h, DIALOG_BG);

    // Title (centered).
    UIRenderer::drawText(opts.title_font, opts.title, dx + (dlg_w - tsz.width) * 0.5f, dy + pad,
                         TITLE_COLOR);

    // Body lines (centered).
    float by = dy + pad + title_h + section_gap;
    for (const auto& line : opts.body_lines)
    {
        const TextSize lsz = UIRenderer::measureText(opts.body_font, line);
        UIRenderer::drawText(opts.body_font, line, dx + (dlg_w - lsz.width) * 0.5f, by, BODY_COLOR);
        by += body_line_h;
    }

    // Buttons.
    const float btn_y = dy + dlg_h - pad - btn_h;
    const float yes_x = dx + (dlg_w - btn_w * 2.0f - btn_gap) * 0.5f;
    const float no_x = yes_x + btn_w + btn_gap;

    // Yes button.
    const bool yesHover = (mx >= yes_x && mx < yes_x + btn_w && my >= btn_y && my < btn_y + btn_h);
    if (yesHover)
        sel = 0;
    UIRenderer::drawRect(yes_x, btn_y, btn_w, btn_h, (sel == 0) ? YES_BG_HL : YES_BG);
    const TextSize ysz = UIRenderer::measureText(opts.title_font, "Yes");
    UIRenderer::drawText(opts.title_font, "Yes", yes_x + (btn_w - ysz.width) * 0.5f,
                         btn_y + (btn_h - ysz.height) * 0.5f, (sel == 0) ? TEXT_WHITE : BTN_NORMAL);
    if (yesHover && mouseClicked(em, SDL_BUTTON_LEFT))
        result = Result::Yes;

    // No button.
    const bool noHover = (mx >= no_x && mx < no_x + btn_w && my >= btn_y && my < btn_y + btn_h);
    if (noHover)
        sel = 1;
    UIRenderer::drawRect(no_x, btn_y, btn_w, btn_h, (sel == 1) ? BTN_BG_HL : BTN_BG);
    const TextSize nsz = UIRenderer::measureText(opts.title_font, "No");
    UIRenderer::drawText(opts.title_font, "No", no_x + (btn_w - nsz.width) * 0.5f,
                         btn_y + (btn_h - nsz.height) * 0.5f, (sel == 1) ? BTN_HOVER : BTN_NORMAL);
    if (noHover && mouseClicked(em, SDL_BUTTON_LEFT))
        result = Result::No;

    return result;
}
