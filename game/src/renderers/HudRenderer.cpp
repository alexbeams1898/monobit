#include "renderers/HudRenderer.h"

#include "SaveManager.h"
#include "UIRenderer.h"
#include "ecs/Components.h"
#include "ecs/GameComponents.h"
#include "ecs/GameConfig.h"
#include "renderers/ItemStatRenderer.h"

#include <SDL.h>
#include <algorithm>
#include <cstdio>
#include <string>
#include <tracy/Tracy.hpp>

static FontHandle sBodyFont = INVALID_FONT;
static FontHandle sTitleFont = INVALID_FONT;
static TextureManager* sTexMgr = nullptr;

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
static constexpr Color WPN_BAR{0.45f, 0.55f, 0.85f, 0.9f};
static constexpr Color WPN_BG{0.12f, 0.15f, 0.30f, 0.6f};
static constexpr Color TEXT_WHITE{1.0f, 1.0f, 1.0f, 1.0f};
static constexpr Color TEXT_GOLD{1.0f, 0.85f, 0.3f, 1.0f};
static constexpr Color MONEY_GREEN{0.2f, 0.85f, 0.3f, 1.0f};
static constexpr Color PANEL_BG{0.0f, 0.0f, 0.0f, 0.4f};
static constexpr Color SECTION_SEP{0.4f, 0.38f, 0.30f, 0.35f};
static constexpr Color SECTION_LABEL{0.6f, 0.58f, 0.50f, 0.7f};

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
    if (ws.phase == WaveState::Phase::Complete)
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

void HudRenderer::init(FontHandle body_font, FontHandle title_font, TextureManager* tm)
{
    sBodyFont = body_font;
    sTitleFont = title_font;
    sTexMgr = tm;
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
        const float title_h = FontManager::lineHeight(sTitleFont);
        // Each section = label + 2px + bar.
        const float section_h = label_h + 2.0f + BAR_H;

        // Character name heading: portrait icon scaled to match title font.
        const float portrait_sz = title_h;
        const float char_h = title_h + BAR_GAP;

        // Weapon XP bar shown only when a weapon is equipped.
        const bool has_wpn_xp = em.registry().all_of<WeaponXP, Weapon>(entity);
        // Weapon section: separator + title-font name + gap + bar section.
        const float sep_h =
            has_wpn_xp ? (1.0f + BAR_GAP + title_h + BAR_GAP + section_h + BAR_GAP) : 0.0f;

        // Money line height (always shown if wallet exists).
        const bool has_wallet = em.registry().all_of<Wallet>(entity);
        const float money_h = has_wallet ? (label_h + BAR_GAP) : 0.0f;

        // Panel: char name + bars + status + optional alloc hint + weapon section + money.
        const float status_h = label_h + BAR_GAP;
        const float alloc_h = (exp.stat_points > 0) ? (label_h + BAR_GAP) : 0.0f;
        const float panel_h = PADDING * 2.0f + char_h + section_h * 3.0f + BAR_GAP * 2.0f +
                              status_h + alloc_h + sep_h + money_h;
        UIRenderer::drawRect(BAR_X - PADDING, BAR_Y_START - PADDING, BAR_W + PADDING * 2.0f,
                             panel_h, PANEL_BG);

        float y = BAR_Y_START;

        // Character name heading with player head icon.
        {
            const auto& gs = em.registry().ctx().get<GameState>();
            float name_x = BAR_X;
            if (sTexMgr != nullptr)
            {
                static const std::string headPath = "assets/sprites/player_upper.png";
                const uint32_t tex = sTexMgr->load(headPath);
                if (tex != 0)
                {
                    int sheetW = 0;
                    int sheetH = 0;
                    sTexMgr->getDimensions(headPath, sheetW, sheetH);
                    // South-facing idle frame 0: crop center of upper half.
                    const float fw = 64.0f;      // frame width
                    const float fh = 64.0f;      // frame height
                    const float crop = 24.0f;    // square crop size in frame px
                    const float cx = fw * 0.5f;  // frame center x
                    const float cy = fh * 0.15f; // head near top of frame
                    const float u0 = (cx - crop * 0.5f) / static_cast<float>(sheetW);
                    const float v0 = cy / static_cast<float>(sheetH);
                    const float u1 = (cx + crop * 0.5f) / static_cast<float>(sheetW);
                    const float v1 = (cy + crop) / static_cast<float>(sheetH);
                    UIRenderer::drawTexturedRect(BAR_X, y, portrait_sz, portrait_sz, tex, u0, v0,
                                                 u1, v1, {1.0f, 1.0f, 1.0f, 1.0f});
                    name_x = BAR_X + portrait_sz + 4.0f;
                }
            }
            UIRenderer::drawText(sTitleFont, gs.active_character, name_x, y, TEXT_GOLD);
            y += char_h;
        }

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

        // XP bar (next to HP -- both are character progression).
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

        // Stamina bar (next to Status -- both are combat condition).
        if (em.registry().all_of<Stamina>(entity))
        {
            const auto& sta = em.registry().get<Stamina>(entity);
            const float fill = sta.max_stamina > 0.0f ? sta.current / sta.max_stamina : 0.0f;
            const int pct = static_cast<int>(fill * 100.0f);
            const std::string label = "STA " + std::to_string(pct) + "%";
            drawBarWithLabel(BAR_X, y, BAR_W, BAR_H, fill, STA_BAR, STA_BG, sBodyFont, label);
            y += section_h + BAR_GAP;
        }

        // Status condition (grouped with player bars).
        renderStatusCondition(em, entity, BAR_X, y);
        y += label_h + BAR_GAP;

        // Money (player resource, grouped with player stats).
        if (has_wallet)
        {
            const int money = em.registry().get<Wallet>(entity).money;
            static constexpr Color MONEY_LABEL{0.6f, 0.58f, 0.52f, 0.9f};
            UIRenderer::drawText(sBodyFont, "Money: ", BAR_X, y, MONEY_LABEL);
            TextSize mlsz = UIRenderer::measureText(sBodyFont, "Money: ");
            UIRenderer::drawText(sBodyFont, "$" + std::to_string(money), BAR_X + mlsz.width, y,
                                 MONEY_GREEN);
            y += label_h + BAR_GAP;
        }

        // Stat allocation hint (only when points are available).
        if (exp.stat_points > 0)
        {
            UIRenderer::drawText(sBodyFont, "Level Up! [Tab]", BAR_X, y, TEXT_GOLD);
            y += label_h + BAR_GAP;
        }

        // Weapon section -- separated from player stats.
        if (has_wpn_xp)
        {
            UIRenderer::drawRect(BAR_X, y, BAR_W, 1.0f, SECTION_SEP);
            y += 1.0f + BAR_GAP;
            const auto& w = em.registry().get<Weapon>(entity);
            const auto& equip = em.registry().get<Equipment>(entity);
            const ItemDef* wpnDef =
                em.registry().ctx().get<ItemRegistry>().find(equip.main_hand.config_path);
            const float icon_sz = title_h;
            ItemStatRenderer::drawItemIcon(wpnDef, BAR_X, y, icon_sz);
            const float name_x =
                (wpnDef && !wpnDef->icon_path.empty()) ? BAR_X + icon_sz + 4.0f : BAR_X;
            UIRenderer::drawText(sTitleFont, w.name, name_x, y, TEXT_GOLD);
            y += title_h + BAR_GAP;

            const auto& wxp = em.registry().get<WeaponXP>(entity);
            const float fill = wxp.xp_to_next > 0.0f ? wxp.current_xp / wxp.xp_to_next : 0.0f;
            const int xp_cur = static_cast<int>(wxp.current_xp);
            const int xp_max = static_cast<int>(wxp.xp_to_next);
            const std::string label = "Lv" + std::to_string(wxp.level) + "  " +
                                      std::to_string(xp_cur) + "/" + std::to_string(xp_max);
            drawBarWithLabel(BAR_X, y, BAR_W, BAR_H, fill, WPN_BAR, WPN_BG, sBodyFont, label);
            y += section_h;
        }

        // Score (bottom-left) -- gothic framed counter with rolling animation.
        {
            const auto& stats = em.registry().ctx().get<RunStats>();
            const auto& cfg = em.registry().ctx().get<ScoringConfig>();
            const int actualScore = SaveManager::computeScore(stats, cfg, false);

            static int sDisplayed = 0;
            static float sFlashTimer = 0.0f;
            const bool ticking = sDisplayed < actualScore;
            if (ticking)
            {
                const int diff = actualScore - sDisplayed;
                const int step = std::max(1, diff / 15);
                sDisplayed = std::min(sDisplayed + step, actualScore);
                sFlashTimer = 0.25f;
            }
            else if (sDisplayed > actualScore)
            {
                sDisplayed = actualScore;
            }
            sFlashTimer = std::max(0.0f, sFlashTimer - 0.016f);

            const std::string scoreStr = std::to_string(sDisplayed);
            TextSize numSz = UIRenderer::measureText(sTitleFont, scoreStr);
            TextSize lblSz = UIRenderer::measureText(sTitleFont, "SCORE");

            // Panel dimensions — minimum width keeps the box punchy at low scores.
            const float contentW = std::max(numSz.width, lblSz.width);
            const float pad = 14.0f;
            const float minW = BAR_W * 0.6f;
            const float panelW = std::max(minW, contentW + pad * 2.0f);
            const float panelH = lblSz.height + 4.0f + numSz.height + pad * 2.0f;
            const float panelX = BAR_X;
            const float panelY = wh - panelH - 16.0f;

            // Dark backdrop.
            static constexpr Color SCORE_BG{0.02f, 0.01f, 0.04f, 0.7f};
            UIRenderer::drawRect(panelX, panelY, panelW, panelH, SCORE_BG);

            // Gold border.
            static constexpr Color BORDER{0.65f, 0.50f, 0.20f, 0.6f};
            static constexpr Color BORDER_HL{0.90f, 0.75f, 0.30f, 0.85f};
            const float flash = sFlashTimer * 4.0f;
            const Color bc{BORDER.r + (BORDER_HL.r - BORDER.r) * flash,
                           BORDER.g + (BORDER_HL.g - BORDER.g) * flash,
                           BORDER.b + (BORDER_HL.b - BORDER.b) * flash,
                           BORDER.a + (BORDER_HL.a - BORDER.a) * flash};
            const float b = 1.5f;
            UIRenderer::drawRect(panelX, panelY, panelW, b, bc);
            UIRenderer::drawRect(panelX, panelY + panelH - b, panelW, b, bc);
            UIRenderer::drawRect(panelX, panelY, b, panelH, bc);
            UIRenderer::drawRect(panelX + panelW - b, panelY, b, panelH, bc);

            // Corner accents (small squares at each corner).
            const float cs = 4.0f;
            UIRenderer::drawRect(panelX - 1.0f, panelY - 1.0f, cs, cs, bc);
            UIRenderer::drawRect(panelX + panelW - cs + 1.0f, panelY - 1.0f, cs, cs, bc);
            UIRenderer::drawRect(panelX - 1.0f, panelY + panelH - cs + 1.0f, cs, cs, bc);
            UIRenderer::drawRect(panelX + panelW - cs + 1.0f, panelY + panelH - cs + 1.0f, cs, cs,
                                 bc);

            // "SCORE" label, centered.
            const float lblX = panelX + (panelW - lblSz.width) * 0.5f;
            const float lblY = panelY + pad;
            static constexpr Color LBL_COLOR{0.55f, 0.45f, 0.30f, 0.85f};
            UIRenderer::drawText(sTitleFont, "SCORE", lblX, lblY, LBL_COLOR);

            // Score number, centered, with shadow + glow.
            const float numX = panelX + (panelW - numSz.width) * 0.5f;
            const float numY = lblY + lblSz.height + 4.0f;

            // Shadow.
            static constexpr Color SHADOW{0.0f, 0.0f, 0.0f, 0.6f};
            UIRenderer::drawText(sTitleFont, scoreStr, numX + 2.0f, numY + 2.0f, SHADOW);

            // Glow layer (wider, faint gold behind the number).
            const Color glowColor{1.0f, 0.7f, 0.15f, 0.15f + flash * 0.2f};
            UIRenderer::drawText(sTitleFont, scoreStr, numX - 1.0f, numY, glowColor);
            UIRenderer::drawText(sTitleFont, scoreStr, numX + 1.0f, numY, glowColor);

            // Main number.
            const Color scoreColor{1.0f, 0.85f + flash * 0.15f, 0.3f + flash * 0.7f, 1.0f};
            UIRenderer::drawText(sTitleFont, scoreStr, numX, numY, scoreColor);
        }

        // Wave info (top-right).
        renderWaveInfo(em, ww);

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
