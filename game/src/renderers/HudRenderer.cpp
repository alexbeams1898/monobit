#include "renderers/HudRenderer.h"

#include "UIRenderer.h"
#include "ecs/Components.h"
#include "ecs/GameComponents.h"
#include "ecs/GameConfig.h"

#include <SDL.h>
#include <algorithm>
#include <cstdio>
#include <string>
#include <tracy/Tracy.hpp>

static FontHandle sBodyFont = INVALID_FONT;
static FontHandle sTitleFont = INVALID_FONT;

// Layout constants.
static constexpr float BAR_X = 20.0f;
static constexpr float BAR_W = 220.0f;
static constexpr float BAR_H = 14.0f;
static constexpr float BAR_GAP = 8.0f;
static constexpr float BAR_Y_START = 20.0f;
static constexpr float PADDING = 12.0f;
static constexpr float TEXT_LEFT_PAD = 6.0f;

// Colors.
static constexpr Color HP_BAR{0.65f, 0.12f, 0.12f, 0.9f};
static constexpr Color HP_BG{0.25f, 0.05f, 0.05f, 0.6f};
static constexpr Color STA_BAR{0.15f, 0.55f, 0.50f, 0.9f};
static constexpr Color STA_BG{0.05f, 0.20f, 0.18f, 0.6f};
static constexpr Color XP_BAR{0.80f, 0.62f, 0.15f, 0.9f};
static constexpr Color XP_BG{0.28f, 0.22f, 0.05f, 0.6f};
static constexpr Color TEXT_WHITE{1.0f, 1.0f, 1.0f, 1.0f};
static constexpr Color TEXT_GOLD{1.0f, 0.85f, 0.3f, 1.0f};
static constexpr Color MONEY_GREEN{0.2f, 0.85f, 0.3f, 1.0f};
static constexpr Color PANEL_BG{0.0f, 0.0f, 0.0f, 0.4f};

static void drawBar(float x, float y, float w, float h, float fill, const Color& fg,
                    const Color& bg)
{
    UIRenderer::drawRect(x, y, w, h, bg);
    UIRenderer::drawRect(x, y, w * std::clamp(fill, 0.0f, 1.0f), h, fg);
}

static void drawBarWithLabel(float x, float y, float w, float h, float fill, const Color& fg,
                             const Color& bg, FontHandle font, const std::string& label)
{
    // Label above the bar.
    UIRenderer::drawText(font, label, x + TEXT_LEFT_PAD, y, TEXT_WHITE);
    const float bar_y = y + FontManager::lineHeight(font) + 2.0f;
    drawBar(x, bar_y, w, h, fill, fg, bg);
}

static void renderWaveInfo(EntityManager& em, float ww)
{
    const auto& ws = em.registry().ctx().get<WaveState>();
    const auto& wc = em.registry().ctx().get<WaveConfig>();
    if (!wc.loaded)
        return;

    std::string waveStr = "Wave " + std::to_string(ws.current_wave);
    if (wc.gen.max_waves > 0)
        waveStr += "/" + std::to_string(wc.gen.max_waves);

    std::string phaseStr;
    if (ws.phase == WaveState::Phase::Idle || ws.phase == WaveState::Phase::SafeRoom)
        phaseStr = "Press R to start";
    else if (ws.phase == WaveState::Phase::GameOver)
        phaseStr = "GAME OVER - Press R";
    else if (ws.phase == WaveState::Phase::Complete)
        phaseStr = "COMPLETE";

    const float title_h = FontManager::lineHeight(sTitleFont);
    TextSize waveSz = UIRenderer::measureText(sTitleFont, waveStr);
    UIRenderer::drawText(sTitleFont, waveStr, ww - waveSz.width - BAR_X, BAR_Y_START, TEXT_GOLD);

    if (!phaseStr.empty())
    {
        TextSize phaseSz = UIRenderer::measureText(sBodyFont, phaseStr);
        UIRenderer::drawText(sBodyFont, phaseStr, ww - phaseSz.width - BAR_X,
                             BAR_Y_START + title_h + BAR_GAP, TEXT_WHITE);
    }
}

static void renderStatusCondition(EntityManager& em, entt::entity entity, float x, float y)
{
    std::string status = "Good";
    Color statusColor{0.4f, 0.8f, 0.45f, 1.0f};

    if (em.registry().all_of<Staggered>(entity))
    {
        status = "Staggered";
        statusColor = {0.85f, 0.35f, 0.35f, 1.0f};
    }
    else if (em.registry().all_of<Stamina>(entity) && em.registry().all_of<Weapon>(entity))
    {
        const auto& sta = em.registry().get<Stamina>(entity);
        const auto& w = em.registry().get<Weapon>(entity);
        const auto& f = em.registry().ctx().get<FormulaConfig>();
        const float swingCost = f.stamina.base_swing_cost + w.weight * f.stamina.swing_effort;
        if (sta.max_stamina < swingCost)
        {
            status = "Overburdened";
            statusColor = {0.85f, 0.5f, 0.2f, 1.0f};
        }
        else if (sta.current < swingCost)
        {
            status = "Exhausted";
            statusColor = {0.85f, 0.65f, 0.2f, 1.0f};
        }
        else if (sta.current < sta.max_stamina * 0.4f)
        {
            status = "Tired";
            statusColor = {0.75f, 0.75f, 0.3f, 1.0f};
        }
    }

    UIRenderer::drawText(sBodyFont, "Status: " + status, x, y, statusColor);
}

void HudRenderer::init(FontHandle body_font, FontHandle title_font)
{
    sBodyFont = body_font;
    sTitleFont = title_font;
}

void HudRenderer::render(EntityManager& em, int window_w, int window_h)
{
    ZoneScopedN("HudRenderer");

    const auto& ui = em.registry().ctx().get<UIState>();
    if (!ui.show_hud)
        return;

    const float ww = static_cast<float>(window_w);
    const float wh = static_cast<float>(window_h);

    for (auto [entity, actions, health, stats, exp] :
         em.registry().view<PlayerActions, Health, Stats, Experience>().each())
    {
        const float label_h = FontManager::lineHeight(sBodyFont);
        // Each section = label + 2px + bar.
        const float section_h = label_h + 2.0f + BAR_H;

        // Panel: includes bars + status line + optional allocation hint.
        const float status_h = label_h + BAR_GAP;
        const float alloc_h = (exp.stat_points > 0) ? (label_h + BAR_GAP) : 0.0f;
        const float panel_h =
            PADDING * 2.0f + section_h * 3.0f + BAR_GAP * 2.0f + status_h + alloc_h;
        UIRenderer::drawRect(BAR_X - PADDING, BAR_Y_START - PADDING, BAR_W + PADDING * 2.0f,
                             panel_h, PANEL_BG);

        float y = BAR_Y_START;

        // HP bar.
        {
            const float fill =
                health.max > 0 ? static_cast<float>(health.current) / static_cast<float>(health.max)
                               : 0.0f;
            const std::string label =
                "HP " + std::to_string(health.current) + "/" + std::to_string(health.max);
            drawBarWithLabel(BAR_X, y, BAR_W, BAR_H, fill, HP_BAR, HP_BG, sBodyFont, label);
            y += section_h + BAR_GAP;
        }

        // Stamina bar.
        if (em.registry().all_of<Stamina>(entity))
        {
            const auto& sta = em.registry().get<Stamina>(entity);
            const float fill = sta.max_stamina > 0.0f ? sta.current / sta.max_stamina : 0.0f;
            const int pct = static_cast<int>(fill * 100.0f);
            const std::string label = "STA " + std::to_string(pct) + "%";
            drawBarWithLabel(BAR_X, y, BAR_W, BAR_H, fill, STA_BAR, STA_BG, sBodyFont, label);
            y += section_h + BAR_GAP;
        }

        // XP bar.
        {
            const float fill = exp.xp_to_next > 0 ? static_cast<float>(exp.current_xp) /
                                                        static_cast<float>(exp.xp_to_next)
                                                  : 0.0f;
            const std::string label = "LVL " + std::to_string(exp.level) + "  XP " +
                                      std::to_string(exp.current_xp) + "/" +
                                      std::to_string(exp.xp_to_next);
            drawBarWithLabel(BAR_X, y, BAR_W, BAR_H, fill, XP_BAR, XP_BG, sBodyFont, label);
            y += section_h + BAR_GAP;
        }

        // Time (bottom-left, above money).
        {
            const auto& runStats = em.registry().ctx().get<RunStats>();
            const int totalSec = static_cast<int>(runStats.time);
            char timeBuf[16];
            std::snprintf(timeBuf, sizeof(timeBuf), "%d:%02d", totalSec / 60, totalSec % 60);
            static constexpr Color TIME_LABEL{0.6f, 0.58f, 0.52f, 0.9f};
            static constexpr Color TIME_VALUE{0.85f, 0.82f, 0.75f, 1.0f};
            const std::string tlabel = "Time: ";
            UIRenderer::drawText(sTitleFont, tlabel, BAR_X, wh - 80.0f, TIME_LABEL);
            TextSize tlsz = UIRenderer::measureText(sTitleFont, tlabel);
            UIRenderer::drawText(sTitleFont, timeBuf, BAR_X + tlsz.width, wh - 80.0f, TIME_VALUE);
        }

        // Money (bottom-left).
        if (em.registry().all_of<Wallet>(entity))
        {
            const int money = em.registry().get<Wallet>(entity).money;
            static constexpr Color MONEY_LABEL{0.6f, 0.58f, 0.52f, 0.9f};
            const std::string mlabel = "Money: ";
            const std::string mvalue = "$" + std::to_string(money);
            UIRenderer::drawText(sTitleFont, mlabel, BAR_X, wh - 40.0f, MONEY_LABEL);
            TextSize mlsz = UIRenderer::measureText(sTitleFont, mlabel);
            UIRenderer::drawText(sTitleFont, mvalue, BAR_X + mlsz.width, wh - 40.0f, MONEY_GREEN);
        }

        // Wave info (top-right).
        renderWaveInfo(em, ww);

        // Status condition.
        renderStatusCondition(em, entity, BAR_X, y);
        y += label_h + BAR_GAP;

        // Stat allocation hint (only when points are available).
        if (exp.stat_points > 0)
        {
            UIRenderer::drawText(sBodyFont, "Level Up! [Tab]", BAR_X, y, TEXT_GOLD);
        }

        break; // only one player
    }
}

bool HudRenderer::renderMenuButton(EntityManager& em, int window_w, int window_h)
{
    const float ww = static_cast<float>(window_w);
    const float wh = static_cast<float>(window_h);

    const std::string label = "Menu [Tab]";
    TextSize sz = UIRenderer::measureText(sTitleFont, label);
    const float pad = 6.0f;
    const float bx = ww - sz.width - pad * 2.0f - 20.0f;
    const float by = wh - sz.height - pad * 2.0f - 20.0f;
    const float bw = sz.width + pad * 2.0f;
    const float bh = sz.height + pad * 2.0f;

    int mouseX = 0;
    int mouseY = 0;
    SDL_GetMouseState(&mouseX, &mouseY);
    const float mx = static_cast<float>(mouseX);
    const float my = static_cast<float>(mouseY);

    bool hovered = (mx >= bx && mx < bx + bw && my >= by && my < by + bh);

    static constexpr Color BTN_BG{0.08f, 0.07f, 0.12f, 0.7f};
    static constexpr Color BTN_BG_HL{0.20f, 0.17f, 0.30f, 0.85f};
    static constexpr Color BTN_BORDER{0.75f, 0.65f, 0.35f, 0.6f};
    static constexpr Color BTN_BORDER_HL{0.95f, 0.85f, 0.45f, 0.9f};
    static constexpr Color TXT_NORM{0.78f, 0.72f, 0.55f, 0.9f};
    static constexpr Color TXT_HL{0.95f, 0.90f, 0.60f, 1.0f};

    UIRenderer::drawRect(bx, by, bw, bh, hovered ? BTN_BG_HL : BTN_BG);

    // Border.
    const float b = 1.5f;
    const Color& bc = hovered ? BTN_BORDER_HL : BTN_BORDER;
    UIRenderer::drawRect(bx, by, bw, b, bc);
    UIRenderer::drawRect(bx, by + bh - b, bw, b, bc);
    UIRenderer::drawRect(bx, by, b, bh, bc);
    UIRenderer::drawRect(bx + bw - b, by, b, bh, bc);

    UIRenderer::drawText(sTitleFont, label, bx + pad, by + pad, hovered ? TXT_HL : TXT_NORM);

    if (hovered)
    {
        for (uint8_t btn : em.mouse_down_events)
        {
            if (btn == SDL_BUTTON_LEFT)
                return true;
        }
    }
    return false;
}
