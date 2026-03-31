#include "screens/LevelUpScreen.h"

#include "UIRenderer.h"
#include "ecs/Components.h"
#include "ecs/GameComponents.h"
#include "ecs/GameConfig.h"
#include "screens/ScreenColors.h"
#include "screens/ScreenInput.h"
#include "systems/LevelingSystem.h"
#include "systems/NotificationSystem.h"

#include <SDL.h>
#include <algorithm>
#include <string>
#include <tracy/Tracy.hpp>

using screen_input::hoveredRow;
using namespace screen_colors;

static FontHandle sBodyFont = INVALID_FONT;
static FontHandle sTitleFont = INVALID_FONT;
static int sSel = -1;
static float sAutoCloseTimer = -1.0f;
static uint32_t sLastTicks = 0;
static int sLevelOnOpen = 0;

static constexpr int STAT_COUNT = 4;
static const char* STAT_NAMES[STAT_COUNT] = {"Strength", "Dexterity", "Endurance", "Luck"};
static const char* STAT_DESCS[STAT_COUNT] = {"Damage, carry weight", "Speed, attack speed",
                                             "HP, stamina, poise", "Drop rate, item quality"};

static constexpr Color TITLE_COLOR{1.0f, 0.85f, 0.3f, 1.0f};
static constexpr Color STAT_COLOR{0.65f, 0.75f, 0.9f, 1.0f};
static constexpr Color SELECTED_BG{0.25f, 0.22f, 0.38f, 0.6f};
static constexpr Color SEP_COLOR{0.4f, 0.35f, 0.25f, 0.5f};
static constexpr Color HINT_COLOR{0.5f, 0.48f, 0.46f, 0.8f};

// Returns true if the screen was auto-closed (caller should return early).
static bool handleAutoClose(UIState& ui, const Experience& exp, float dt)
{
    if (exp.stat_points <= 0)
    {
        if (sAutoCloseTimer < 0.0f)
            sAutoCloseTimer = 0.5f;
        sAutoCloseTimer -= dt;
        if (sAutoCloseTimer <= 0.0f)
        {
            NotificationSystem::push("Level Up! (Lv " + std::to_string(sLevelOnOpen) + ")",
                                     {1.0f, 0.85f, 0.3f, 1.0f});
            sLevelOnOpen = 0;
            ui.active_screen = UIState::Screen::None;
            return true;
        }
    }
    else
    {
        sAutoCloseTimer = -1.0f;
    }
    return false;
}

static void renderStatRows(EntityManager& em, entt::entity player, Stats& stats,
                           const Experience& exp, const FormulaConfig& f, const SoundConfig& snd,
                           float cx, float cw, float& y, float line_h, float mx, float my)
{
    const int hover = hoveredRow(mx, my, cx, y, cw, line_h, STAT_COUNT);
    if (hover >= 0)
        sSel = hover;
    else if (em.key_down_events.empty())
        sSel = -1;

    int* stat_ptrs[STAT_COUNT] = {&stats.str, &stats.dex, &stats.end, &stats.lck};

    for (int i = 0; i < STAT_COUNT; ++i)
    {
        const bool selected = (i == sSel);

        if (selected)
            UIRenderer::drawRect(cx - 4.0f, y - 2.0f, cw + 8.0f, line_h, SELECTED_BG);

        const std::string prefix = selected ? "> " : "  ";
        UIRenderer::drawText(sBodyFont,
                             prefix + STAT_NAMES[i] + "  " + std::to_string(*stat_ptrs[i]), cx, y,
                             selected ? TEXT_WHITE : STAT_COLOR);

        // Description on the right.
        const float desc_x = cx + 220.0f;
        UIRenderer::drawText(sBodyFont, STAT_DESCS[i], desc_x, y, TEXT_DIM);

        // Mouse click allocates.
        if (selected && exp.stat_points > 0)
        {
            if (mx >= cx - 4.0f && mx < cx + cw + 4.0f && my >= y - 2.0f && my < y - 2.0f + line_h)
            {
                for (const uint8_t btn : em.mouse_down_events)
                {
                    if (btn == SDL_BUTTON_LEFT)
                        allocateStat(em.registry(), player, *stat_ptrs[i], f, snd);
                }
            }
        }

        y += line_h;
    }
}

void LevelUpScreen::init(FontHandle body_font, FontHandle title_font)
{
    sBodyFont = body_font;
    sTitleFont = title_font;
}

void LevelUpScreen::reset()
{
    sSel = -1;
    sAutoCloseTimer = -1.0f;
    sLastTicks = 0;
}

void LevelUpScreen::render(EntityManager& em, int window_w, int window_h)
{
    ZoneScopedN("LevelUpScreen");

    const float ww = static_cast<float>(window_w);
    const float wh = static_cast<float>(window_h);
    auto& ui = em.registry().ctx().get<UIState>();

    // Delta time via SDL ticks.
    const uint32_t now = SDL_GetTicks();
    const float dt = (sLastTicks > 0) ? static_cast<float>(now - sLastTicks) / 1000.0f : 0.0f;
    sLastTicks = now;

    // Find player.
    entt::entity player = entt::null;
    for (auto e : em.registry().view<PlayerActions>())
    {
        player = e;
        break;
    }
    if (player == entt::null || !em.registry().all_of<Stats, Experience>(player))
    {
        ui.active_screen = UIState::Screen::None;
        return;
    }

    auto& stats = em.registry().get<Stats>(player);
    const auto& exp = em.registry().get<Experience>(player);
    const auto& f = em.registry().ctx().get<FormulaConfig>();
    const auto& snd = em.registry().ctx().get<SoundConfig>();

    // Capture level on first frame for the close notification.
    if (sLevelOnOpen == 0)
        sLevelOnOpen = exp.level;

    // Auto-close when all points spent.
    if (handleAutoClose(ui, exp, dt))
        return;

    // Mouse position.
    int mouseX = 0, mouseY = 0;
    SDL_GetMouseState(&mouseX, &mouseY);
    const float mx = static_cast<float>(mouseX);
    const float my = static_cast<float>(mouseY);

    // Layout.
    const float panel_w = 520.0f;
    const float panel_h = 340.0f;
    const float panel_x = (ww - panel_w) * 0.5f;
    const float panel_y = (wh - panel_h) * 0.5f;
    const float pad = 20.0f;
    const float cx = panel_x + pad;
    const float cw = panel_w - pad * 2.0f;
    const float line_h = FontManager::lineHeight(sBodyFont) + 6.0f;
    const float title_h = FontManager::lineHeight(sTitleFont);

    // Draw overlay + panel.
    UIRenderer::drawRect(0.0f, 0.0f, ww, wh, OVERLAY);
    UIRenderer::drawRect(panel_x, panel_y, panel_w, panel_h, PANEL_BG);

    float y = panel_y + pad;

    // Title.
    const std::string title = "Level Up!";
    const TextSize tsz = UIRenderer::measureText(sTitleFont, title);
    UIRenderer::drawText(sTitleFont, title, panel_x + (panel_w - tsz.width) * 0.5f, y, TITLE_COLOR);
    y += title_h + 4.0f;

    // Points remaining.
    const std::string pts = "You have " + std::to_string(exp.stat_points) + " stat point" +
                            (exp.stat_points != 1 ? "s" : "") + ".";
    const TextSize psz = UIRenderer::measureText(sBodyFont, pts);
    UIRenderer::drawText(sBodyFont, pts, panel_x + (panel_w - psz.width) * 0.5f, y, TEXT_WHITE);
    y += line_h + 4.0f;

    // Separator.
    UIRenderer::drawRect(cx, y, cw, 1.0f, SEP_COLOR);
    y += 10.0f;

    // Stat rows.
    renderStatRows(em, player, stats, exp, f, snd, cx, cw, y, line_h, mx, my);

    int* stat_ptrs[STAT_COUNT] = {&stats.str, &stats.dex, &stats.end, &stats.lck};

    // Separator.
    y += 6.0f;
    UIRenderer::drawRect(cx, y, cw, 1.0f, SEP_COLOR);
    y += 10.0f;

    // Hint.
    const std::string hint = "[F] Allocate   [Tab/ESC] Close";
    const TextSize hsz = UIRenderer::measureText(sBodyFont, hint);
    UIRenderer::drawText(sBodyFont, hint, panel_x + (panel_w - hsz.width) * 0.5f, y, HINT_COLOR);

    // Keyboard input.
    for (const int key : em.key_down_events)
    {
        if (key == SDL_SCANCODE_UP || key == SDL_SCANCODE_W)
            sSel = sSel < 0 ? 0 : ((sSel - 1) + STAT_COUNT) % STAT_COUNT;
        else if (key == SDL_SCANCODE_DOWN || key == SDL_SCANCODE_S)
            sSel = sSel < 0 ? 0 : (sSel + 1) % STAT_COUNT;
        else if (sSel >= 0 &&
                 (key == SDL_SCANCODE_RETURN || key == SDL_SCANCODE_KP_ENTER ||
                  key == SDL_SCANCODE_F) &&
                 exp.stat_points > 0)
            allocateStat(em.registry(), player, *stat_ptrs[sSel], f, snd);
    }
}
