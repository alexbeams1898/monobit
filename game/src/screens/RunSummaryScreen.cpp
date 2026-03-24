#include "screens/RunSummaryScreen.h"

#include "UIRenderer.h"
#include "ecs/EntityManager.h"
#include "ecs/GameConfig.h"
#include "systems/AudioSystem.h"

#include <SDL.h>
#include <algorithm>
#include <cstdio>
#include <string>
#include <tracy/Tracy.hpp>

static FontHandle sBodyFont = INVALID_FONT;
static FontHandle sTitleFont = INVALID_FONT;
static bool sEscaped = false;
static int sScore = 0;
static bool sIsHighScore = false;

static constexpr Color OVERLAY{0.0f, 0.0f, 0.0f, 0.92f};
static constexpr Color TITLE_COLOR{0.9f, 0.78f, 0.45f, 1.0f};
static constexpr Color TEXT_WHITE{0.92f, 0.90f, 0.88f, 1.0f};
static constexpr Color TEXT_DIM{0.5f, 0.48f, 0.46f, 1.0f};
static constexpr Color LABEL_COLOR{0.55f, 0.7f, 0.85f, 1.0f};
static constexpr Color GOLD{0.95f, 0.85f, 0.4f, 1.0f};
static constexpr Color PANEL_BG{0.06f, 0.06f, 0.09f, 0.92f};
static constexpr Color BTN_NORMAL{0.7f, 0.68f, 0.65f, 1.0f};
static constexpr Color BTN_HOVER{0.95f, 0.88f, 0.55f, 1.0f};
static constexpr Color BTN_BG{0.1f, 0.1f, 0.12f, 0.5f};
static constexpr Color BTN_BG_HL{0.18f, 0.16f, 0.25f, 0.7f};

static bool mouseClicked(const EntityManager& em)
{
    for (uint8_t btn : em.mouse_down_events)
        if (btn == SDL_BUTTON_LEFT)
            return true;
    return false;
}

static bool keyPressed(const EntityManager& em, int scancode)
{
    const auto& kd = em.key_down_events;
    return std::find(kd.begin(), kd.end(), scancode) != kd.end();
}

void RunSummaryScreen::init(FontHandle body_font, FontHandle title_font)
{
    sBodyFont = body_font;
    sTitleFont = title_font;
}

void RunSummaryScreen::reset(bool escaped, int score, bool is_high_score)
{
    sEscaped = escaped;
    sScore = score;
    sIsHighScore = is_high_score;
}

bool RunSummaryScreen::render(EntityManager& em, int window_w, int window_h)
{
    ZoneScopedN("RunSummaryScreen");

    const float ww = static_cast<float>(window_w);
    const float wh = static_cast<float>(window_h);
    const auto& stats = em.registry().ctx().get<RunStats>();
    const auto& snd = em.registry().ctx().get<SoundConfig>();

    UIRenderer::drawRect(0.0f, 0.0f, ww, wh, OVERLAY);

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
    TextSize tsz = UIRenderer::measureText(sTitleFont, title);
    UIRenderer::drawText(sTitleFont, title, px + (panel_w - tsz.width) * 0.5f, y, TITLE_COLOR);
    y += FontManager::lineHeight(sTitleFont) + 16.0f;

    // Outcome.
    const std::string outcome = sEscaped ? "ESCAPED" : "DIED";
    const Color outcomeColor = sEscaped ? GOLD : Color{0.9f, 0.2f, 0.15f, 1.0f};
    TextSize osz = UIRenderer::measureText(sTitleFont, outcome);
    UIRenderer::drawText(sTitleFont, outcome, px + (panel_w - osz.width) * 0.5f, y, outcomeColor);
    y += FontManager::lineHeight(sTitleFont) + 16.0f;

    // Stats.
    UIRenderer::drawText(sBodyFont, "Wave Reached", cx, y, LABEL_COLOR);
    UIRenderer::drawText(sBodyFont, std::to_string(stats.wave), val_x, y, TEXT_WHITE);
    y += line_h;

    UIRenderer::drawText(sBodyFont, "Kills", cx, y, LABEL_COLOR);
    UIRenderer::drawText(sBodyFont, std::to_string(stats.kills), val_x, y, TEXT_WHITE);
    y += line_h;

    // Time as MM:SS.
    const int totalSec = static_cast<int>(stats.time);
    char timeBuf[16];
    std::snprintf(timeBuf, sizeof(timeBuf), "%d:%02d", totalSec / 60, totalSec % 60);
    UIRenderer::drawText(sBodyFont, "Time", cx, y, LABEL_COLOR);
    UIRenderer::drawText(sBodyFont, timeBuf, val_x, y, TEXT_WHITE);
    y += line_h;

    UIRenderer::drawText(sBodyFont, "XP Earned", cx, y, LABEL_COLOR);
    UIRenderer::drawText(sBodyFont, std::to_string(stats.xp_earned), val_x, y, TEXT_WHITE);
    y += line_h;

    UIRenderer::drawText(sBodyFont, "Money", cx, y, LABEL_COLOR);
    UIRenderer::drawText(sBodyFont, "$" + std::to_string(stats.money), val_x, y, TEXT_WHITE);
    y += line_h + 8.0f;

    // Score.
    UIRenderer::drawText(sTitleFont, "Score", cx, y, LABEL_COLOR);
    UIRenderer::drawText(sTitleFont, std::to_string(sScore), val_x, y, GOLD);
    y += FontManager::lineHeight(sTitleFont) + 8.0f;

    // High score flash.
    if (sIsHighScore)
    {
        const std::string hs = "HIGH SCORE!";
        TextSize hsz = UIRenderer::measureText(sTitleFont, hs);
        const float blink = ((SDL_GetTicks() / 400) % 2 == 0) ? 1.0f : 0.6f;
        UIRenderer::drawText(sTitleFont, hs, px + (panel_w - hsz.width) * 0.5f, y,
                             {GOLD.r, GOLD.g, GOLD.b, blink});
    }

    // Continue button.
    const float btn_y = py + panel_h - 60.0f;
    const std::string cont = "Continue";
    TextSize csz = UIRenderer::measureText(sTitleFont, cont);
    const float bw = csz.width + 60.0f;
    const float bh = csz.height + 20.0f;
    const float bx = (ww - bw) * 0.5f;

    int mouseX = 0;
    int mouseY = 0;
    SDL_GetMouseState(&mouseX, &mouseY);
    const float mx = static_cast<float>(mouseX);
    const float my = static_cast<float>(mouseY);

    const bool hovered = (mx >= bx && mx < bx + bw && my >= btn_y && my < btn_y + bh);
    UIRenderer::drawRect(bx, btn_y, bw, bh, hovered ? BTN_BG_HL : BTN_BG);
    UIRenderer::drawText(sTitleFont, cont, bx + 30.0f, btn_y + 10.0f,
                         hovered ? BTN_HOVER : BTN_NORMAL);

    if ((hovered && mouseClicked(em)) || keyPressed(em, SDL_SCANCODE_RETURN) ||
        keyPressed(em, SDL_SCANCODE_KP_ENTER))
    {
        if (!snd.ui_click.path.empty())
            AudioSystem::playSfx(snd.ui_click.path, snd.ui_click.volume);
        return true;
    }

    return false;
}
