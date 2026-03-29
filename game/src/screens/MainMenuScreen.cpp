#include "screens/MainMenuScreen.h"

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
static int sSel = -1;

static constexpr Color OVERLAY{0.0f, 0.0f, 0.0f, 0.92f};
static constexpr Color TITLE_COLOR{0.9f, 0.78f, 0.45f, 1.0f};
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

void MainMenuScreen::init(FontHandle body_font, FontHandle title_font, FontHandle big_title_font)
{
    sBodyFont = body_font;
    sTitleFont = title_font;
    sBigTitleFont = big_title_font;
}

void MainMenuScreen::reset()
{
    sSel = -1;
}

MainMenuScreen::Action MainMenuScreen::render(EntityManager& em, int window_w, int window_h)
{
    ZoneScopedN("MainMenuScreen");

    const float ww = static_cast<float>(window_w);
    const float wh = static_cast<float>(window_h);
    const auto& saveData = em.registry().ctx().get<SaveData>();
    const auto& snd = em.registry().ctx().get<SoundConfig>();

    // Determine visible buttons. Load Game only if characters exist.
    const bool hasChars = !saveData.characters.empty();

    static constexpr Action kActionsWithLoad[] = {Action::NewGame, Action::LoadGame,
                                                  Action::HighScores, Action::Quit};
    static constexpr Action kActionsNoLoad[] = {Action::NewGame, Action::HighScores, Action::Quit};
    static constexpr const char* kLabelsWithLoad[] = {"New Game", "Load Game", "High Scores",
                                                      "Quit"};
    static constexpr const char* kLabelsNoLoad[] = {"New Game", "High Scores", "Quit"};

    const Action* actions = hasChars ? kActionsWithLoad : kActionsNoLoad;
    const char* const* labels = hasChars ? kLabelsWithLoad : kLabelsNoLoad;
    const int btnCount = hasChars ? 4 : 3;

    // Keyboard nav -- first press snaps to item 0 if nothing selected.
    if (keyPressed(em, SDL_SCANCODE_UP) || keyPressed(em, SDL_SCANCODE_W))
        sSel = sSel < 0 ? 0 : (sSel - 1 + btnCount) % btnCount;
    if (keyPressed(em, SDL_SCANCODE_DOWN) || keyPressed(em, SDL_SCANCODE_S))
        sSel = sSel < 0 ? 0 : (sSel + 1) % btnCount;

    Action result = Action::None;
    if (sSel >= 0 &&
        (keyPressed(em, SDL_SCANCODE_RETURN) || keyPressed(em, SDL_SCANCODE_KP_ENTER)))
    {
        result = actions[sSel];
        if (result != Action::None && !snd.ui_click.path.empty())
            AudioSystem::playSfx(snd.ui_click.path, snd.ui_click.volume);
    }

    // Draw.
    UIRenderer::drawRect(0.0f, 0.0f, ww, wh, OVERLAY);

    // Title.
    const std::string title = "HELL ESCAPE";
    TextSize tsz = UIRenderer::measureText(sBigTitleFont, title);
    UIRenderer::drawText(sBigTitleFont, title, (ww - tsz.width) * 0.5f, wh * 0.2f, TITLE_COLOR);

    // Buttons.
    int mouseX = 0;
    int mouseY = 0;
    SDL_GetMouseState(&mouseX, &mouseY);
    const float mx = static_cast<float>(mouseX);
    const float my = static_cast<float>(mouseY);

    const float btn_pad_x = 30.0f;
    const float btn_pad_y = 10.0f;
    const float btn_gap = 12.0f;
    const float line_h = FontManager::lineHeight(sTitleFont);
    const float btn_h = line_h + btn_pad_y * 2.0f;
    const float total_h =
        static_cast<float>(btnCount) * btn_h + static_cast<float>(btnCount - 1) * btn_gap;
    float by = (wh - total_h) * 0.5f + wh * 0.05f;

    bool anyHovered = false;
    for (int i = 0; i < btnCount; ++i)
    {
        const std::string label = labels[i];
        TextSize sz = UIRenderer::measureText(sTitleFont, label);
        const float bw = sz.width + btn_pad_x * 2.0f;
        const float bx = (ww - bw) * 0.5f;

        const bool hovered = (mx >= bx && mx < bx + bw && my >= by && my < by + btn_h);
        if (hovered)
        {
            sSel = i;
            anyHovered = true;
        }

        const bool selected = (i == sSel);
        UIRenderer::drawRect(bx, by, bw, btn_h, selected ? BTN_BG_HL : BTN_BG);
        UIRenderer::drawText(sTitleFont, label, bx + btn_pad_x, by + btn_pad_y,
                             selected ? BTN_HOVER : BTN_NORMAL);

        if (hovered && mouseClicked(em))
        {
            result = actions[i];
            if (result != Action::None && !snd.ui_click.path.empty())
                AudioSystem::playSfx(snd.ui_click.path, snd.ui_click.volume);
        }

        by += btn_h + btn_gap;
    }
    if (!anyHovered && !em.key_down_events.empty())
    {
        // Keyboard took over -- keep sSel.
    }
    else if (!anyHovered)
    {
        sSel = -1;
    }

    return result;
}
