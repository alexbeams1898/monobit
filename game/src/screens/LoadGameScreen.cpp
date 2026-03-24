#include "screens/LoadGameScreen.h"

#include "UIRenderer.h"
#include "ecs/EntityManager.h"
#include "ecs/GameConfig.h"
#include "systems/AudioSystem.h"

#include <SDL.h>
#include <algorithm>
#include <string>
#include <tracy/Tracy.hpp>

static FontHandle sBodyFont = INVALID_FONT;
static FontHandle sTitleFont = INVALID_FONT;
static FontHandle sBigTitleFont = INVALID_FONT;
static int sSel = 0;
static std::string sSelectedName;

static constexpr Color OVERLAY{0.0f, 0.0f, 0.0f, 0.92f};
static constexpr Color TITLE_COLOR{0.9f, 0.78f, 0.45f, 1.0f};
static constexpr Color PANEL_BG{0.06f, 0.06f, 0.09f, 0.92f};
static constexpr Color TEXT_WHITE{0.92f, 0.90f, 0.88f, 1.0f};
static constexpr Color TEXT_DIM{0.5f, 0.48f, 0.46f, 1.0f};
static constexpr Color SELECTED_BG{0.25f, 0.22f, 0.38f, 0.6f};
static constexpr Color HOVER_BG{0.15f, 0.13f, 0.22f, 0.4f};
static constexpr Color BTN_NORMAL{0.7f, 0.68f, 0.65f, 1.0f};
static constexpr Color BTN_HOVER{0.95f, 0.88f, 0.55f, 1.0f};
static constexpr Color BTN_BG{0.1f, 0.1f, 0.12f, 0.5f};
static constexpr Color BTN_BG_HL{0.18f, 0.16f, 0.25f, 0.7f};

static bool keyPressed(const EntityManager& em, int scancode)
{
    const auto& kd = em.key_down_events;
    return std::find(kd.begin(), kd.end(), scancode) != kd.end();
}

static bool mouseClicked(const EntityManager& em)
{
    for (uint8_t btn : em.mouse_down_events)
        if (btn == SDL_BUTTON_LEFT)
            return true;
    return false;
}

void LoadGameScreen::init(FontHandle body_font, FontHandle title_font, FontHandle big_title_font)
{
    sBodyFont = body_font;
    sTitleFont = title_font;
    sBigTitleFont = big_title_font;
}

void LoadGameScreen::reset()
{
    sSel = 0;
    sSelectedName.clear();
}

const char* LoadGameScreen::getSelectedName()
{
    return sSelectedName.c_str();
}

// Handle keyboard input and return any action triggered.
static LoadGameScreen::Action handleLoadInput(const EntityManager& em, const SoundConfig& snd,
                                              const SaveData& saveData)
{
    const int charCount = static_cast<int>(saveData.characters.size());
    const int totalItems = charCount + 1;

    if (keyPressed(em, SDL_SCANCODE_UP) || keyPressed(em, SDL_SCANCODE_W))
        sSel = (sSel - 1 + totalItems) % totalItems;
    if (keyPressed(em, SDL_SCANCODE_DOWN) || keyPressed(em, SDL_SCANCODE_S))
        sSel = (sSel + 1) % totalItems;

    if (keyPressed(em, SDL_SCANCODE_ESCAPE))
        return LoadGameScreen::Action::Back;

    if (!keyPressed(em, SDL_SCANCODE_RETURN) && !keyPressed(em, SDL_SCANCODE_KP_ENTER))
        return LoadGameScreen::Action::None;

    LoadGameScreen::Action result = LoadGameScreen::Action::None;
    if (sSel < charCount)
    {
        sSelectedName = saveData.characters[static_cast<size_t>(sSel)].name;
        result = LoadGameScreen::Action::Select;
    }
    else
    {
        result = LoadGameScreen::Action::Back;
    }
    if (!snd.ui_click.path.empty())
        AudioSystem::playSfx(snd.ui_click.path, snd.ui_click.volume);
    return result;
}

LoadGameScreen::Action LoadGameScreen::render(EntityManager& em, int window_w, int window_h)
{
    ZoneScopedN("LoadGameScreen");

    const float ww = static_cast<float>(window_w);
    const float wh = static_cast<float>(window_h);
    const auto& saveData = em.registry().ctx().get<SaveData>();
    const auto& snd = em.registry().ctx().get<SoundConfig>();
    const int charCount = static_cast<int>(saveData.characters.size());

    Action result = handleLoadInput(em, snd, saveData);

    // Draw.
    UIRenderer::drawRect(0.0f, 0.0f, ww, wh, OVERLAY);

    // Title (big font).
    const std::string title = "Load Game";
    TextSize tsz = UIRenderer::measureText(sBigTitleFont, title);
    UIRenderer::drawText(sBigTitleFont, title, (ww - tsz.width) * 0.5f, wh * 0.12f, TITLE_COLOR);

    // Subtitle.
    const std::string subtitle = "Select a character";
    TextSize ssz = UIRenderer::measureText(sBodyFont, subtitle);
    UIRenderer::drawText(sBodyFont, subtitle, (ww - ssz.width) * 0.5f,
                         wh * 0.12f + FontManager::lineHeight(sBigTitleFont) + 8.0f, TEXT_DIM);

    int mouseX = 0;
    int mouseY = 0;
    SDL_GetMouseState(&mouseX, &mouseY);
    const float mx = static_cast<float>(mouseX);
    const float my = static_cast<float>(mouseY);

    // Character list panel.
    const float row_pad = 12.0f;
    const float row_h = FontManager::lineHeight(sTitleFont) + row_pad * 2.0f;
    const float list_w = 460.0f;
    const float list_h = static_cast<float>(charCount) * (row_h + 6.0f) + 40.0f;
    const float panel_h = list_h + row_h + 60.0f; // room for back button
    const float lx = (ww - list_w) * 0.5f;
    const float panel_y = wh * 0.32f;

    UIRenderer::drawRect(lx - 20.0f, panel_y - 20.0f, list_w + 40.0f, panel_h, PANEL_BG);

    float ly = panel_y;

    for (int i = 0; i < charCount; ++i)
    {
        const bool selected = (i == sSel);
        const bool hovered = (mx >= lx && mx < lx + list_w && my >= ly && my < ly + row_h);
        if (hovered)
            sSel = i;

        // Row background.
        if (selected)
            UIRenderer::drawRect(lx, ly, list_w, row_h, SELECTED_BG);
        else if (hovered)
            UIRenderer::drawRect(lx, ly, list_w, row_h, HOVER_BG);

        // Character name (title font for bigger text).
        UIRenderer::drawText(sTitleFont, saveData.characters[static_cast<size_t>(i)].name,
                             lx + 16.0f, ly + row_pad, selected ? TEXT_WHITE : TEXT_DIM);

        if (hovered && mouseClicked(em))
        {
            sSelectedName = saveData.characters[static_cast<size_t>(i)].name;
            result = Action::Select;
            if (!snd.ui_click.path.empty())
                AudioSystem::playSfx(snd.ui_click.path, snd.ui_click.volume);
        }

        ly += row_h + 6.0f;
    }

    // Back button.
    ly += 24.0f;
    const std::string backLabel = "Back";
    TextSize bsz = UIRenderer::measureText(sTitleFont, backLabel);
    const float bw = bsz.width + 60.0f;
    const float bh = bsz.height + 20.0f;
    const float bx = (ww - bw) * 0.5f;

    const bool backSel = (sSel == charCount);
    const bool backHover = (mx >= bx && mx < bx + bw && my >= ly && my < ly + bh);
    if (backHover)
        sSel = charCount;

    UIRenderer::drawRect(bx, ly, bw, bh, backSel ? BTN_BG_HL : BTN_BG);
    UIRenderer::drawText(sTitleFont, backLabel, bx + 30.0f, ly + 10.0f,
                         backSel ? BTN_HOVER : BTN_NORMAL);

    if (backHover && mouseClicked(em))
    {
        result = Action::Back;
        if (!snd.ui_click.path.empty())
            AudioSystem::playSfx(snd.ui_click.path, snd.ui_click.volume);
    }

    return result;
}
