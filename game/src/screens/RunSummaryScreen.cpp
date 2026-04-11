#include "screens/RunSummaryScreen.h"

#include "UIRenderer.h"
#include "ecs/EntityManager.h"
#include "ecs/GameConfig.h"
#include "screens/ScreenColors.h"
#include "screens/ScreenInput.h"
#include "systems/AudioSystem.h"

#include <SDL.h>
#include <string>
#include <tracy/Tracy.hpp>

using screen_input::keyPressed;
using screen_input::mouseClicked;
using namespace screen_colors;

static FontHandle sBodyFont = INVALID_FONT;
static FontHandle sTitleFont = INVALID_FONT;
static bool sEscaped = false;
static int sScore = 0;
static bool sIsHighScore = false;
static bool sGodMode = false;

static constexpr Color TITLE_COLOR{0.9f, 0.78f, 0.45f, 1.0f};
static constexpr Color LABEL_COLOR{0.55f, 0.7f, 0.85f, 1.0f};
static constexpr Color GOLD{0.95f, 0.85f, 0.4f, 1.0f};

void RunSummaryScreen::init(FontHandle body_font, FontHandle title_font)
{
    sBodyFont = body_font;
    sTitleFont = title_font;
}

void RunSummaryScreen::reset(bool escaped, int score, bool is_high_score, bool god_mode)
{
    sEscaped = escaped;
    sScore = score;
    sIsHighScore = is_high_score;
    sGodMode = god_mode;
}

bool RunSummaryScreen::render(EntityManager& em, int window_w, int window_h)
{
    ZoneScopedN("RunSummaryScreen");

    const float ww = static_cast<float>(window_w);
    const float wh = static_cast<float>(window_h);
    const auto& stats = em.registry().ctx().get<RunStats>();
    const auto& snd = em.registry().ctx().get<SoundConfig>();

    UIRenderer::drawRect(0.0f, 0.0f, ww, wh, OVERLAY_OPAQUE);

    // Panel.
    const float panel_w = 450.0f;
    const float panel_h = 480.0f;
    const float px = (ww - panel_w) * 0.5f;
    const float py = (wh - panel_h) * 0.5f;
    UIRenderer::drawRect(px, py, panel_w, panel_h, PANEL_BG);

    const float cx = px + 30.0f;
    const float cw = panel_w - 60.0f;
    float y = py + 20.0f;
    const float line_h = FontManager::lineHeight(sBodyFont) + 6.0f;
    const float val_x = cx + 180.0f;

    // Title.
    const std::string title = "Run Summary";
    const TextSize tsz = UIRenderer::measureText(sTitleFont, title);
    UIRenderer::drawText(sTitleFont, title, px + (panel_w - tsz.width) * 0.5f, y, TITLE_COLOR);
    y += FontManager::lineHeight(sTitleFont) + 16.0f;

    // Outcome.
    const std::string outcome = sEscaped ? "ESCAPED" : "DIED";
    const Color outcomeColor = sEscaped ? GOLD : Color{0.9f, 0.2f, 0.15f, 1.0f};
    const TextSize osz = UIRenderer::measureText(sTitleFont, outcome);
    UIRenderer::drawText(sTitleFont, outcome, px + (panel_w - osz.width) * 0.5f, y, outcomeColor);
    y += FontManager::lineHeight(sTitleFont) + 16.0f;

    // Stats.
    UIRenderer::drawText(sBodyFont, "Wave Reached", cx, y, LABEL_COLOR);
    UIRenderer::drawText(sBodyFont, std::to_string(stats.wave), val_x, y, TEXT_WHITE);
    y += line_h;

    UIRenderer::drawText(sBodyFont, "Kills", cx, y, LABEL_COLOR);
    UIRenderer::drawText(sBodyFont, std::to_string(stats.kills), val_x, y, TEXT_WHITE);
    y += line_h;

    UIRenderer::drawText(sBodyFont, "XP Earned", cx, y, LABEL_COLOR);
    UIRenderer::drawText(sBodyFont, std::to_string(stats.xp_earned), val_x, y, TEXT_WHITE);
    y += line_h;

    UIRenderer::drawText(sBodyFont, "Money", cx, y, LABEL_COLOR);
    UIRenderer::drawText(sBodyFont, "$" + std::to_string(stats.money), val_x, y, TEXT_WHITE);
    y += line_h + 8.0f;

    // Score. In god mode the score is shown dimmed and struck through to make
    // it visually clear the number doesn't count.
    {
        const Color labelColor = sGodMode ? Color{0.4f, 0.4f, 0.4f, 1.0f} : LABEL_COLOR;
        const Color valueColor = sGodMode ? Color{0.5f, 0.5f, 0.5f, 1.0f} : GOLD;
        const std::string scoreStr = std::to_string(sScore);
        UIRenderer::drawText(sTitleFont, "Score", cx, y, labelColor);
        UIRenderer::drawText(sTitleFont, scoreStr, val_x, y, valueColor);
        if (sGodMode)
        {
            // Draw a strikethrough across the value text.
            const TextSize vsz = UIRenderer::measureText(sTitleFont, scoreStr);
            const float strikeY = y + vsz.height * 0.5f - 1.0f;
            UIRenderer::drawRect(val_x - 2.0f, strikeY, vsz.width + 4.0f, 2.0f, valueColor);
        }
    }
    y += FontManager::lineHeight(sTitleFont) + 8.0f;

    // High score flash, or "god mode" notice when god mode was on. The two are
    // mutually exclusive (god runs never qualify for high scores).
    if (sGodMode)
    {
        const std::string note = "GOD MODE DOESN'T COUNT LOL";
        const TextSize nsz = UIRenderer::measureText(sBodyFont, note);
        UIRenderer::drawText(sBodyFont, note, px + (panel_w - nsz.width) * 0.5f, y,
                             Color{0.7f, 0.7f, 0.7f, 1.0f});
    }
    else if (sIsHighScore)
    {
        const std::string hs = "HIGH SCORE!";
        const TextSize hsz = UIRenderer::measureText(sTitleFont, hs);
        const float blink = ((SDL_GetTicks() / 400) % 2 == 0) ? 1.0f : 0.6f;
        UIRenderer::drawText(sTitleFont, hs, px + (panel_w - hsz.width) * 0.5f, y,
                             {GOLD.r, GOLD.g, GOLD.b, blink});
    }

    // Continue button (anchored 20px from panel bottom).
    {
        const std::string cont = "Continue";
        const TextSize csz = UIRenderer::measureText(sTitleFont, cont);
        const float bw = csz.width + 60.0f;
        const float bh = csz.height + 20.0f;
        const float bx = (ww - bw) * 0.5f;
        const float btn_y = py + panel_h - 20.0f - bh;

        int mouseX = 0;
        int mouseY = 0;
        SDL_GetMouseState(&mouseX, &mouseY);
        const float mx = static_cast<float>(mouseX);
        const float my = static_cast<float>(mouseY);

        const bool hovered = (mx >= bx && mx < bx + bw && my >= btn_y && my < btn_y + bh);
        const Color bg = hovered ? BTN_BG_HL : BTN_BG;
        UIRenderer::drawRect(bx, btn_y, bw, bh, bg);
        const Color txt = hovered ? BTN_HOVER : BTN_NORMAL;
        UIRenderer::drawText(sTitleFont, cont, bx + 30.0f, btn_y + 10.0f, txt);

        if ((hovered && mouseClicked(em, SDL_BUTTON_LEFT)) || keyPressed(em, SDL_SCANCODE_RETURN) ||
            keyPressed(em, SDL_SCANCODE_KP_ENTER))
        {
            if (!snd.get("ui_click").path.empty())
                AudioSystem::playSfx(snd.get("ui_click").path, snd.get("ui_click").volume);
            return true;
        }
    }

    return false;
}
