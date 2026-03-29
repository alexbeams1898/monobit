#include "screens/HighScoresScreen.h"

#include "SaveManager.h"
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

static constexpr Color OVERLAY{0.0f, 0.0f, 0.0f, 0.92f};
static constexpr Color TITLE_COLOR{0.9f, 0.78f, 0.45f, 1.0f};
static constexpr Color PANEL_BG{0.06f, 0.06f, 0.09f, 0.92f};
static constexpr Color TEXT_WHITE{0.92f, 0.90f, 0.88f, 1.0f};
static constexpr Color TEXT_DIM{0.5f, 0.48f, 0.46f, 1.0f};
static constexpr Color GOLD{0.95f, 0.85f, 0.4f, 1.0f};
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

void HighScoresScreen::init(FontHandle body_font, FontHandle title_font)
{
    sBodyFont = body_font;
    sTitleFont = title_font;
}

void HighScoresScreen::reset()
{
}

bool HighScoresScreen::render(EntityManager& em, int window_w, int window_h)
{
    ZoneScopedN("HighScoresScreen");

    const float ww = static_cast<float>(window_w);
    const float wh = static_cast<float>(window_h);
    const auto& saveData = em.registry().ctx().get<SaveData>();
    const auto& snd = em.registry().ctx().get<SoundConfig>();

    UIRenderer::drawRect(0.0f, 0.0f, ww, wh, OVERLAY);

    // Panel.
    const float panel_w = 740.0f;
    const float panel_h = 550.0f;
    const float px = (ww - panel_w) * 0.5f;
    const float py = (wh - panel_h) * 0.5f;
    UIRenderer::drawRect(px, py, panel_w, panel_h, PANEL_BG);

    float y = py + 20.0f;
    const float cx = px + 20.0f;

    // Title.
    const std::string title = "High Scores";
    TextSize tsz = UIRenderer::measureText(sTitleFont, title);
    UIRenderer::drawText(sTitleFont, title, px + (panel_w - tsz.width) * 0.5f, y, TITLE_COLOR);
    y += FontManager::lineHeight(sTitleFont) + 16.0f;

    auto top = SaveManager::topRuns(saveData, 10);
    const float line_h = FontManager::lineHeight(sBodyFont) + 6.0f;

    if (top.empty())
    {
        UIRenderer::drawText(sBodyFont, "No runs recorded yet.", cx, y, TEXT_DIM);
    }
    else
    {
        // Header.
        UIRenderer::drawText(sBodyFont, "#", cx, y, TEXT_DIM);
        UIRenderer::drawText(sBodyFont, "Name", cx + 40.0f, y, TEXT_DIM);
        UIRenderer::drawText(sBodyFont, "Score", cx + 200.0f, y, TEXT_DIM);
        UIRenderer::drawText(sBodyFont, "Wave", cx + 310.0f, y, TEXT_DIM);
        UIRenderer::drawText(sBodyFont, "Kills", cx + 390.0f, y, TEXT_DIM);
        UIRenderer::drawText(sBodyFont, "Time", cx + 470.0f, y, TEXT_DIM);
        UIRenderer::drawText(sBodyFont, "Result", cx + 560.0f, y, TEXT_DIM);
        y += line_h;

        for (int i = 0; i < static_cast<int>(top.size()); ++i)
        {
            const auto& run = top[static_cast<size_t>(i)];
            const Color rowColor = (i == 0) ? GOLD : TEXT_WHITE;

            UIRenderer::drawText(sBodyFont, std::to_string(i + 1), cx, y, rowColor);
            UIRenderer::drawText(sBodyFont, run.character_name, cx + 40.0f, y, rowColor);
            UIRenderer::drawText(sBodyFont, std::to_string(run.stats.score), cx + 200.0f, y,
                                 rowColor);
            UIRenderer::drawText(sBodyFont, std::to_string(run.stats.wave), cx + 310.0f, y,
                                 rowColor);
            UIRenderer::drawText(sBodyFont, std::to_string(run.stats.kills), cx + 390.0f, y,
                                 rowColor);
            const int sec = static_cast<int>(run.stats.time);
            char timeBuf[16];
            std::snprintf(timeBuf, sizeof(timeBuf), "%d:%02d", sec / 60, sec % 60);
            UIRenderer::drawText(sBodyFont, timeBuf, cx + 470.0f, y, rowColor);
            UIRenderer::drawText(sBodyFont, run.escaped ? "Escaped" : "Died", cx + 560.0f, y,
                                 run.escaped ? GOLD : Color{0.9f, 0.2f, 0.15f, 1.0f});
            y += line_h;
        }
    }

    // Back button (anchored 20px from panel bottom).
    const std::string backLabel = "Back";
    TextSize bsz = UIRenderer::measureText(sTitleFont, backLabel);
    const float bw = bsz.width + 60.0f;
    const float bh = bsz.height + 20.0f;
    const float bx = (ww - bw) * 0.5f;
    const float btn_y = py + panel_h - 20.0f - bh;

    int mouseX = 0;
    int mouseY = 0;
    SDL_GetMouseState(&mouseX, &mouseY);
    const float mx = static_cast<float>(mouseX);
    const float my = static_cast<float>(mouseY);

    const bool hovered = (mx >= bx && mx < bx + bw && my >= btn_y && my < btn_y + bh);
    UIRenderer::drawRect(bx, btn_y, bw, bh, hovered ? BTN_BG_HL : BTN_BG);
    UIRenderer::drawText(sTitleFont, backLabel, bx + 30.0f, btn_y + 10.0f,
                         hovered ? BTN_HOVER : BTN_NORMAL);

    if ((hovered && mouseClicked(em)) || keyPressed(em, SDL_SCANCODE_ESCAPE) ||
        keyPressed(em, SDL_SCANCODE_RETURN) || keyPressed(em, SDL_SCANCODE_KP_ENTER))
    {
        if (!snd.ui_click.path.empty())
            AudioSystem::playSfx(snd.ui_click.path, snd.ui_click.volume);
        return true;
    }

    return false;
}
