#include "renderers/HudRenderer.h"

#include "SaveManager.h"
#include "UIRenderer.h"
#include "ecs/Components.h"
#include "ecs/GameComponents.h"
#include "ecs/GameConfig.h"
#include "ops/InventoryOps.h"
#include "renderers/ItemStatRenderer.h"
#include "utils/DebugDraw.h"

#include <SDL.h>
#include <algorithm>
#include <cmath>
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
    const TextSize waveSz = UIRenderer::measureText(sTitleFont, waveStr);
    UIRenderer::drawText(sTitleFont, waveStr, ww - waveSz.width - BAR_X, BAR_Y_START, TEXT_GOLD);

    if (!phaseStr.empty())
    {
        const TextSize phaseSz = UIRenderer::measureText(sBodyFont, phaseStr);
        UIRenderer::drawText(sBodyFont, phaseStr, ww - phaseSz.width - BAR_X,
                             BAR_Y_START + title_h + BAR_GAP, TEXT_WHITE);
    }
}

static void renderStatusCondition(EntityManager& em, entt::entity entity, float x, float y)
{
    std::string status = "Good";
    Color statusColor{0.4f, 0.8f, 0.45f, 1.0f};

    if (em.registry().ctx().get<DebugFlags>().god_mode)
    {
        status = "God Mode";
        statusColor = {1.0f, 0.84f, 0.0f, 1.0f};
    }
    else if (em.registry().all_of<Staggered>(entity))
    {
        status = "Staggered";
        statusColor = {0.85f, 0.35f, 0.35f, 1.0f};
    }
    else if (em.registry().all_of<Stamina>(entity) && em.registry().all_of<Weapon>(entity))
    {
        const auto& sta = em.registry().get<Stamina>(entity);
        const auto& w = em.registry().get<Weapon>(entity);
        const auto& f = em.registry().ctx().get<FormulaConfig>();
        const float swingCost = w.stamina_cost >= 0.0f
                                    ? w.stamina_cost
                                    : f.stamina.base_swing_cost + w.weight * f.stamina.swing_effort;
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

static void renderPortraitAndName(EntityManager& em, entt::entity entity, float& y,
                                  float portrait_sz, float char_h)
{
    auto& reg = em.registry();
    const auto& gs = reg.ctx().get<GameState>();
    float name_x = BAR_X;

    // Draw full south-facing idle frame (row 0, col 0) from the player's
    // composited sprite texture. Sheet layout is cols = direction_count *
    // max_frames_per_state, rows = Animation::STATE_COUNT.
    const auto* spr = reg.try_get<Sprite>(entity);
    const auto* anim = reg.try_get<Animation>(entity);
    if (spr != nullptr && anim != nullptr && spr->texture_id != 0)
    {
        const int cols =
            std::max(anim->direction_count, 1) * std::max(anim->max_frames_per_state, 1);
        const int rows = Animation::STATE_COUNT;
        if (cols > 0 && rows > 0)
        {
            const float u1 = 1.0f / static_cast<float>(cols);
            const float v1 = 1.0f / static_cast<float>(rows);
            UIRenderer::drawTexturedRect(BAR_X, y, portrait_sz, portrait_sz, spr->texture_id, 0.0f,
                                         0.0f, u1, v1, {1.0f, 1.0f, 1.0f, 1.0f});
            name_x = BAR_X + portrait_sz + 4.0f;
        }
    }
    UIRenderer::drawText(sTitleFont, gs.active_character, name_x, y, TEXT_GOLD);
    y += char_h;
}

static void renderWeaponSection(EntityManager& em, entt::entity entity, float& y, float title_h)
{
    UIRenderer::drawRect(BAR_X, y, BAR_W, 1.0f, SECTION_SEP);
    y += 1.0f + BAR_GAP;
    const auto& w = em.registry().get<Weapon>(entity);
    const auto& equip = em.registry().get<Equipment>(entity);
    const ItemDef* wpnDef =
        em.registry().ctx().get<ItemRegistry>().find(equip.main_hand.config_path);
    const float icon_sz = title_h;
    ItemStatRenderer::drawItemIcon(wpnDef, BAR_X, y, icon_sz);
    const float name_x = (wpnDef && !wpnDef->icon_path.empty()) ? BAR_X + icon_sz + 4.0f : BAR_X;
    UIRenderer::drawText(sTitleFont, w.name, name_x, y, TEXT_GOLD);
    y += title_h + BAR_GAP;

    // Ammo counter for ranged weapons (inventory pool + optional magazine).
    if (w.ranged && !w.ammo_type.empty())
    {
        static constexpr Color AMMO_ORANGE{1.0f, 0.6f, 0.15f, 1.0f};
        static constexpr Color AMMO_RED{1.0f, 0.2f, 0.2f, 1.0f};

        const auto* inv = em.registry().try_get<Inventory>(entity);
        const int reserve = inv != nullptr ? InventoryOps::countItem(*inv, w.ammo_type) : 0;
        const auto* rs = em.registry().try_get<RangedState>(entity);
        const bool hasMagazine = rs != nullptr && rs->magazine_size > 0;

        std::string ammoStr;
        Color ammoColor = TEXT_WHITE;

        if (hasMagazine)
        {
            if (rs->reloading)
            {
                ammoStr = "RELOADING... (" + std::to_string(reserve) + ")";
                ammoColor = AMMO_ORANGE;
            }
            else
            {
                ammoStr = std::to_string(rs->ammo_in_magazine) + "/" +
                          std::to_string(rs->magazine_size) + " (" + std::to_string(reserve) + ")";
                ammoColor = (rs->ammo_in_magazine > 0 || reserve > 0) ? TEXT_WHITE : AMMO_RED;
            }
        }
        else
        {
            // Bow-type: just show inventory count.
            const auto& items = em.registry().ctx().get<ItemRegistry>();
            const ItemDef* ammoDef = items.find(w.ammo_type);
            const std::string ammoName = ammoDef != nullptr ? ammoDef->name : "Ammo";
            ammoStr = ammoName + ": " + std::to_string(reserve);
            ammoColor = reserve > 0 ? TEXT_WHITE : AMMO_RED;
        }

        UIRenderer::drawText(sBodyFont, ammoStr, BAR_X, y, ammoColor);
        y += BAR_H + BAR_GAP;
    }

    const auto& wxp = em.registry().get<WeaponXP>(entity);
    const float fill = wxp.xp_to_next > 0.0f ? wxp.current_xp / wxp.xp_to_next : 0.0f;
    const int xp_cur = static_cast<int>(wxp.current_xp);
    const int xp_max = static_cast<int>(wxp.xp_to_next);
    const std::string label = "Lv" + std::to_string(wxp.level) + "  " + std::to_string(xp_cur) +
                              "/" + std::to_string(xp_max);
    drawBarWithLabel(BAR_X, y, BAR_W, BAR_H, fill, WPN_BAR, WPN_BG, sBodyFont, label);
}

static void renderScorePanel(EntityManager& em, float wh)
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
    const TextSize numSz = UIRenderer::measureText(sTitleFont, scoreStr);
    const TextSize lblSz = UIRenderer::measureText(sTitleFont, "SCORE");

    const float contentW = std::max(numSz.width, lblSz.width);
    const float pad = 14.0f;
    const float minW = BAR_W * 0.6f;
    const float panelW = std::max(minW, contentW + pad * 2.0f);
    const float panelH = lblSz.height + 4.0f + numSz.height + pad * 2.0f;
    const float panelX = BAR_X;
    const float panelY = wh - panelH - 16.0f;

    static constexpr Color SCORE_BG{0.02f, 0.01f, 0.04f, 0.7f};
    UIRenderer::drawRect(panelX, panelY, panelW, panelH, SCORE_BG);

    static constexpr Color BORDER{0.65f, 0.50f, 0.20f, 0.6f};
    static constexpr Color BORDER_HL{0.90f, 0.75f, 0.30f, 0.85f};
    const float flash = sFlashTimer * 4.0f;
    const Color bc{
        BORDER.r + (BORDER_HL.r - BORDER.r) * flash, BORDER.g + (BORDER_HL.g - BORDER.g) * flash,
        BORDER.b + (BORDER_HL.b - BORDER.b) * flash, BORDER.a + (BORDER_HL.a - BORDER.a) * flash};
    const float b = 1.5f;
    UIRenderer::drawRect(panelX, panelY, panelW, b, bc);
    UIRenderer::drawRect(panelX, panelY + panelH - b, panelW, b, bc);
    UIRenderer::drawRect(panelX, panelY, b, panelH, bc);
    UIRenderer::drawRect(panelX + panelW - b, panelY, b, panelH, bc);

    const float cs = 4.0f;
    UIRenderer::drawRect(panelX - 1.0f, panelY - 1.0f, cs, cs, bc);
    UIRenderer::drawRect(panelX + panelW - cs + 1.0f, panelY - 1.0f, cs, cs, bc);
    UIRenderer::drawRect(panelX - 1.0f, panelY + panelH - cs + 1.0f, cs, cs, bc);
    UIRenderer::drawRect(panelX + panelW - cs + 1.0f, panelY + panelH - cs + 1.0f, cs, cs, bc);

    const float lblX = panelX + (panelW - lblSz.width) * 0.5f;
    const float lblY = panelY + pad;
    static constexpr Color LBL_COLOR{0.55f, 0.45f, 0.30f, 0.85f};
    UIRenderer::drawText(sTitleFont, "SCORE", lblX, lblY, LBL_COLOR);

    const float numX = panelX + (panelW - numSz.width) * 0.5f;
    const float numY = lblY + lblSz.height + 4.0f;

    static constexpr Color SHADOW{0.0f, 0.0f, 0.0f, 0.6f};
    UIRenderer::drawText(sTitleFont, scoreStr, numX + 2.0f, numY + 2.0f, SHADOW);

    const Color glowColor{1.0f, 0.7f, 0.15f, 0.15f + flash * 0.2f};
    UIRenderer::drawText(sTitleFont, scoreStr, numX - 1.0f, numY, glowColor);
    UIRenderer::drawText(sTitleFont, scoreStr, numX + 1.0f, numY, glowColor);

    const Color scoreColor{1.0f, 0.85f + flash * 0.15f, 0.3f + flash * 0.7f, 1.0f};
    UIRenderer::drawText(sTitleFont, scoreStr, numX, numY, scoreColor);
}

void HudRenderer::init(FontHandle body_font, FontHandle title_font, TextureManager* /*tm*/)
{
    sBodyFont = body_font;
    sTitleFont = title_font;
}

static void renderPlayerHud(EntityManager& em, entt::entity entity, float ww, float wh)
{
    auto& reg = em.registry();
    const auto& health = reg.get<Health>(entity);
    const auto& exp = reg.get<Experience>(entity);

    const float label_h = FontManager::lineHeight(sBodyFont);
    const float title_h = FontManager::lineHeight(sTitleFont);
    const float section_h = label_h + 2.0f + BAR_H;
    const float portrait_sz = title_h;
    const float char_h = title_h + BAR_GAP;

    // Measure panel height dynamically so new lines (ammo, etc.) auto-expand.
    float panel_h = PADDING * 2.0f + char_h + section_h * 3.0f + BAR_GAP * 2.0f;
    panel_h += label_h + BAR_GAP; // status
    if (reg.all_of<Wallet>(entity))
        panel_h += label_h + BAR_GAP;
    if (exp.stat_points > 0)
        panel_h += label_h + BAR_GAP;
    if (reg.all_of<WeaponXP, Weapon>(entity))
    {
        panel_h += 1.0f + BAR_GAP;    // separator
        panel_h += title_h + BAR_GAP; // weapon name
        if (reg.all_of<Weapon>(entity) && reg.get<Weapon>(entity).ranged &&
            !reg.get<Weapon>(entity).ammo_type.empty())
            panel_h += BAR_H + BAR_GAP; // ammo line
        panel_h += section_h + BAR_GAP; // weapon XP bar
    }
    UIRenderer::drawRect(BAR_X - PADDING, BAR_Y_START - PADDING, BAR_W + PADDING * 2.0f, panel_h,
                         PANEL_BG);

    float y = BAR_Y_START;
    renderPortraitAndName(em, entity, y, portrait_sz, char_h);

    // HP bar.
    {
        const float fill = health.max > 0
                               ? static_cast<float>(health.current) / static_cast<float>(health.max)
                               : 0.0f;
        const std::string label =
            "HP " + std::to_string(health.current) + "/" + std::to_string(health.max);
        drawBarWithLabel(BAR_X, y, BAR_W, BAR_H, fill, HP_BAR, HP_BG, sBodyFont, label);
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

    // Stamina bar.
    if (reg.all_of<Stamina>(entity))
    {
        const auto& sta = reg.get<Stamina>(entity);
        const float fill = sta.max_stamina > 0.0f ? sta.current / sta.max_stamina : 0.0f;
        const int pct = static_cast<int>(fill * 100.0f);
        const std::string label = "STA " + std::to_string(pct) + "%";
        drawBarWithLabel(BAR_X, y, BAR_W, BAR_H, fill, STA_BAR, STA_BG, sBodyFont, label);
        y += section_h + BAR_GAP;
    }

    renderStatusCondition(em, entity, BAR_X, y);
    y += label_h + BAR_GAP;

    if (reg.all_of<Wallet>(entity))
    {
        const int money = reg.get<Wallet>(entity).money;
        static constexpr Color MONEY_LABEL{0.6f, 0.58f, 0.52f, 0.9f};
        UIRenderer::drawText(sBodyFont, "Money: ", BAR_X, y, MONEY_LABEL);
        const TextSize mlsz = UIRenderer::measureText(sBodyFont, "Money: ");
        UIRenderer::drawText(sBodyFont, "$" + std::to_string(money), BAR_X + mlsz.width, y,
                             MONEY_GREEN);
        y += label_h + BAR_GAP;
    }

    if (exp.stat_points > 0)
    {
        UIRenderer::drawText(sBodyFont, "Level Up! [Tab]", BAR_X, y, TEXT_GOLD);
        y += label_h + BAR_GAP;
    }

    if (reg.all_of<WeaponXP, Weapon>(entity))
        renderWeaponSection(em, entity, y, title_h);

    renderScorePanel(em, wh);
    renderWaveInfo(em, ww);
}

// Pulsing glow on enemies that are backstab/riposte-vulnerable.
static void renderCritIndicators(EntityManager& em, entt::entity entity, float ww, float wh)
{
    auto& reg = em.registry();
    const auto& cam = reg.get<Camera>(entity);
    const auto& pt = reg.get<Transform>(entity);
    const bool hasRiposte = reg.all_of<RiposteWindow>(entity);

    const float ticks = static_cast<float>(SDL_GetTicks());
    const float pulse = 0.5f + 0.5f * std::sin(ticks * 0.01f);

    for (auto [eEnemy, ai, et] : reg.view<AIController, Transform>().each())
    {
        if (reg.all_of<Dead>(eEnemy))
            continue;

        bool showCrit = hasRiposte && reg.all_of<Staggered>(eEnemy);

        if (!showCrit && reg.all_of<FacingDirection>(eEnemy))
        {
            const auto& ef = reg.get<FacingDirection>(eEnemy);
            const float toAtkX = pt.x - et.x;
            const float toAtkY = pt.y - et.y;
            const float len = std::sqrt(toAtkX * toAtkX + toAtkY * toAtkY);
            if (len > 0.0f && len < 80.0f)
            {
                const float dot = (toAtkX / len) * ef.dx + (toAtkY / len) * ef.dy;
                showCrit = (dot <= -0.3f && ai.state != AIController::State::Attack);
            }
        }

        if (!showCrit)
            continue;

        const float z = DebugDraw::sZoom;
        const float ex = (et.x - cam.x) * z + ww * 0.5f;
        const float ey = (et.y - cam.y) * z + wh * 0.5f;
        const float r = 10.0f + pulse * 4.0f;
        const float alpha = 0.4f + pulse * 0.3f;
        static constexpr Color CRIT_GLOW_BASE{1.0f, 0.3f, 0.1f, 1.0f};
        const Color glow{CRIT_GLOW_BASE.r, CRIT_GLOW_BASE.g, CRIT_GLOW_BASE.b, alpha};

        UIRenderer::drawRect(ex - r, ey - 1.5f, r * 2.0f, 3.0f, glow);
        UIRenderer::drawRect(ex - 1.5f, ey - r, 3.0f, r * 2.0f, glow);

        const float d = r * 0.7f;
        UIRenderer::drawRect(ex - d - 1.0f, ey - d - 1.0f, 3.0f, 3.0f, glow);
        UIRenderer::drawRect(ex + d - 1.0f, ey - d - 1.0f, 3.0f, 3.0f, glow);
        UIRenderer::drawRect(ex - d - 1.0f, ey + d - 1.0f, 3.0f, 3.0f, glow);
        UIRenderer::drawRect(ex + d - 1.0f, ey + d - 1.0f, 3.0f, 3.0f, glow);
    }
}

void HudRenderer::render(EntityManager& em, int window_w, int window_h)
{
    ZoneScopedN("HudRenderer");

    const auto& ui = em.registry().ctx().get<UIState>();
    if (!ui.show_hud)
        return;

    const float ww = static_cast<float>(window_w);
    const float wh = static_cast<float>(window_h);

    for (auto entity : em.registry().view<PlayerActions, Health, Stats, Experience>())
    {
        renderPlayerHud(em, entity, ww, wh);

        // Critical opportunity spotlight: pulsing glow on enemy center.
        const bool hasCamera = em.registry().all_of<Camera>(entity);
        if (hasCamera && em.registry().all_of<Transform, FacingDirection>(entity))
            renderCritIndicators(em, entity, ww, wh);
        break;
    }
}

bool HudRenderer::renderMenuButton(EntityManager& em, int window_w, int window_h)
{
    const float ww = static_cast<float>(window_w);
    const float wh = static_cast<float>(window_h);

    const std::string label = "Menu [Tab]";
    const TextSize sz = UIRenderer::measureText(sTitleFont, label);
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

    const bool hovered = (mx >= bx && mx < bx + bw && my >= by && my < by + bh);

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
        for (const uint8_t btn : em.mouse_down_events)
        {
            if (btn == SDL_BUTTON_LEFT)
                return true;
        }
    }
    return false;
}
