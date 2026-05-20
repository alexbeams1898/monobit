#include "screens/ControlsScreen.h"

#include "TextureManager.h"
#include "UIRenderer.h"
#include "ecs/AppState.h"
#include "ecs/EntityManager.h"
#include "ecs/GameConfig.h"
#include "screens/MainMenuScreen.h"
#include "screens/ScreenColors.h"
#include "screens/ScreenInput.h"

#include <tracy/Tracy.hpp>

#include <SDL.h>

#include <string>

using screen_input::keyPressed;
using screen_input::mouseClicked;
using namespace screen_colors;

static FontHandle sBodyFont = INVALID_FONT;
static FontHandle sTitleFont = INVALID_FONT;
static TextureManager* sTexMgr = nullptr;

static constexpr Color TITLE_COLOR{0.9f, 0.78f, 0.45f, 1.0f};
static constexpr Color KEY_COLOR{0.9f, 0.82f, 0.5f, 1.0f};
static constexpr Color DESC_COLOR{0.75f, 0.75f, 0.75f, 1.0f};

void ControlsScreen::init(FontHandle body_font, FontHandle title_font, TextureManager* tm)
{
    sBodyFont = body_font;
    sTitleFont = title_font;
    sTexMgr = tm;
}

void ControlsScreen::reset()
{
}

// Draw a mouse icon inline at (x,y) scaled to iconH. Returns width consumed.
static float drawMouseIcon(const std::string& path, float x, float y, float iconH)
{
    if (sTexMgr == nullptr)
        return 0.0f;
    const uint32_t tex = sTexMgr->load(path);
    if (tex == 0)
        return 0.0f;
    int tw = 0;
    int th = 0;
    sTexMgr->getDimensions(path, tw, th);
    if (tw == 0 || th == 0)
        return 0.0f;
    const float scale = iconH / static_cast<float>(th);
    const float iconW = static_cast<float>(tw) * scale;
    UIRenderer::drawTexturedRect(Rect{x, y, iconW, iconH}, tex);
    return iconW;
}

struct ControlEntry
{
    const char* key;
    const char* mouse_icon;
    const char* description;
};

static constexpr const char* LMB_PATH = "assets/sprites/ui/mouse_lmb.png";
static constexpr const char* RMB_PATH = "assets/sprites/ui/mouse_rmb.png";

static const ControlEntry kControls[] = {
    {"W A S D", nullptr, "Move"},
    {"Shift", nullptr, "Sprint"},
    {"Space", nullptr, "Dodge"},
    {nullptr, LMB_PATH, "Left hand attack"},
    {nullptr, RMB_PATH, "Right hand attack"},
    {"Ctrl", nullptr, "Weapon skill"},
    {"R", nullptr, "Reload"},
    {"C / V", nullptr, "Cycle right hand weapon"},
    {"Z / X", nullptr, "Cycle left hand weapon"},
    {"Alt", nullptr, "Toggle two-hand mode"},
    {"Tab", nullptr, "Inventory"},
    {"Esc", nullptr, "Pause menu"},
    {"F", nullptr, "Pick up / interact"},
};

static constexpr int kControlCount = sizeof(kControls) / sizeof(kControls[0]);

void ControlsScreen::render(EntityManager& em, int window_w, int window_h)
{
    ZoneScopedN("ControlsScreen");

    const float ww = static_cast<float>(window_w);
    const float wh = static_cast<float>(window_h);

    if (keyPressed(em, SDL_SCANCODE_ESCAPE) || keyPressed(em, SDL_SCANCODE_BACKSPACE))
    {
        screen_input::playClickSfx(em);
        MainMenuScreen::reset();
        em.registry().ctx().get<GameState>().phase = GameState::Phase::MainMenu;
        return;
    }

    UIRenderer::drawRect(0.0f, 0.0f, ww, wh, OVERLAY_OPAQUE);

    // Title.
    const std::string title = "Controls";
    const TextSize tsz = UIRenderer::measureText(sTitleFont, title);
    const float titleY = wh * 0.08f;
    UIRenderer::drawText(sTitleFont, title, (ww - tsz.width) * 0.5f, titleY, TITLE_COLOR);

    // Two-column layout: keys right-aligned, descriptions left-aligned.
    const float lineH = FontManager::lineHeight(sBodyFont);
    const float rowH = lineH * 1.6f;
    const float iconH = lineH * 1.2f;
    const float startY = titleY + tsz.height + 30.0f;
    const float colGap = 30.0f;
    const float keyColRight = ww * 0.5f - colGap * 0.5f;
    const float descColLeft = ww * 0.5f + colGap * 0.5f;

    for (int i = 0; i < kControlCount; ++i)
    {
        const float y = startY + static_cast<float>(i) * rowH;
        const auto& entry = kControls[i];

        if (entry.mouse_icon != nullptr)
        {
            // Measure icon width first, then draw right-aligned.
            int tw = 0;
            int th = 0;
            if (sTexMgr != nullptr)
                sTexMgr->getDimensions(entry.mouse_icon, tw, th);
            const float iw =
                (th > 0) ? (static_cast<float>(tw) * iconH / static_cast<float>(th)) : 0.0f;
            drawMouseIcon(entry.mouse_icon, keyColRight - iw, y + (lineH - iconH) * 0.5f, iconH);
        }
        else
        {
            const std::string keyStr = entry.key;
            const TextSize ksz = UIRenderer::measureText(sBodyFont, keyStr);
            UIRenderer::drawText(sBodyFont, keyStr, keyColRight - ksz.width, y, KEY_COLOR);
        }

        UIRenderer::drawText(sBodyFont, entry.description, descColLeft, y, DESC_COLOR);
    }

    // Back button.
    const float totalH = static_cast<float>(kControlCount) * rowH;
    const float backY = startY + totalH + 30.0f;
    const std::string backLabel = "Back";
    const TextSize bsz = UIRenderer::measureText(sTitleFont, backLabel);
    const float btnPadX = 30.0f;
    const float btnPadY = 10.0f;
    const float btnW = bsz.width + btnPadX * 2.0f;
    const float btnH = bsz.height + btnPadY * 2.0f;
    const float btnX = (ww - btnW) * 0.5f;

    int mouseX = 0;
    int mouseY = 0;
    SDL_GetMouseState(&mouseX, &mouseY);
    const float mx = static_cast<float>(mouseX);
    const float my = static_cast<float>(mouseY);
    const bool hovered = (mx >= btnX && mx < btnX + btnW && my >= backY && my < backY + btnH);

    UIRenderer::drawRect(btnX, backY, btnW, btnH, hovered ? BTN_BG_HL : BTN_BG);
    UIRenderer::drawText(sTitleFont, backLabel, btnX + btnPadX, backY + btnPadY,
                         hovered ? BTN_HOVER : BTN_NORMAL);

    if (hovered && mouseClicked(em, SDL_BUTTON_LEFT))
    {
        screen_input::playClickSfx(em);
        MainMenuScreen::reset();
        em.registry().ctx().get<GameState>().phase = GameState::Phase::MainMenu;
    }
}
