#include "screens/CharCreateScreen.h"

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
static std::string sName;
static int sSel = 0; // 0 = Start, 1 = Back
static constexpr int MAX_NAME_LEN = 20;

static constexpr Color OVERLAY{0.0f, 0.0f, 0.0f, 0.92f};
static constexpr Color TITLE_COLOR{0.9f, 0.78f, 0.45f, 1.0f};
static constexpr Color TEXT_WHITE{0.92f, 0.90f, 0.88f, 1.0f};
static constexpr Color TEXT_DIM{0.5f, 0.48f, 0.46f, 1.0f};
static constexpr Color FIELD_BG{0.12f, 0.12f, 0.15f, 0.9f};
static constexpr Color FIELD_BORDER{0.5f, 0.45f, 0.3f, 0.8f};
static constexpr Color CURSOR_COLOR{0.9f, 0.78f, 0.45f, 1.0f};
static constexpr Color BTN_NORMAL{0.7f, 0.68f, 0.65f, 1.0f};
static constexpr Color BTN_HOVER{0.95f, 0.88f, 0.55f, 1.0f};
static constexpr Color BTN_DIM{0.35f, 0.35f, 0.35f, 0.6f};
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

static bool isNameValid()
{
    for (char c : sName)
        if (c != ' ')
            return true;
    return false;
}

void CharCreateScreen::init(FontHandle body_font, FontHandle title_font)
{
    sBodyFont = body_font;
    sTitleFont = title_font;
}

void CharCreateScreen::reset()
{
    sName.clear();
    sSel = 0;
    SDL_StartTextInput();
}

const char* CharCreateScreen::getName()
{
    return sName.c_str();
}

// Handle keyboard input and return any action triggered by keys.
static CharCreateScreen::Action handleCharCreateInput(const EntityManager& em,
                                                      const SoundConfig& snd)
{
    // Text input.
    for (char c : em.text_input_buffer)
    {
        if (static_cast<int>(sName.size()) < MAX_NAME_LEN && c >= 32)
            sName += c;
    }
    if (keyPressed(em, SDL_SCANCODE_BACKSPACE) && !sName.empty())
        sName.pop_back();

    // Tab/arrow toggles between Start and Back.
    if (keyPressed(em, SDL_SCANCODE_TAB) || keyPressed(em, SDL_SCANCODE_DOWN) ||
        keyPressed(em, SDL_SCANCODE_UP))
        sSel = (sSel + 1) % 2;

    if (keyPressed(em, SDL_SCANCODE_ESCAPE))
    {
        SDL_StopTextInput();
        return CharCreateScreen::Action::Back;
    }

    if (!keyPressed(em, SDL_SCANCODE_RETURN) && !keyPressed(em, SDL_SCANCODE_KP_ENTER))
        return CharCreateScreen::Action::None;

    CharCreateScreen::Action result = CharCreateScreen::Action::None;
    if (sSel == 0 && isNameValid())
        result = CharCreateScreen::Action::Start;
    else if (sSel == 1)
        result = CharCreateScreen::Action::Back;

    if (result != CharCreateScreen::Action::None)
    {
        SDL_StopTextInput();
        if (!snd.ui_click.path.empty())
            AudioSystem::playSfx(snd.ui_click.path, snd.ui_click.volume);
    }
    return result;
}

CharCreateScreen::Action CharCreateScreen::render(EntityManager& em, int window_w, int window_h)
{
    ZoneScopedN("CharCreateScreen");

    const float ww = static_cast<float>(window_w);
    const float wh = static_cast<float>(window_h);
    const auto& snd = em.registry().ctx().get<SoundConfig>();

    Action result = handleCharCreateInput(em, snd);

    // Draw.
    UIRenderer::drawRect(0.0f, 0.0f, ww, wh, OVERLAY);

    const std::string title = "Create Character";
    TextSize tsz = UIRenderer::measureText(sTitleFont, title);
    UIRenderer::drawText(sTitleFont, title, (ww - tsz.width) * 0.5f, wh * 0.25f, TITLE_COLOR);

    // Name field.
    const float field_w = 360.0f;
    const float field_h = 48.0f;
    const float fx = (ww - field_w) * 0.5f;
    const float fy = wh * 0.38f;

    UIRenderer::drawText(sBodyFont, "Name:", fx, fy - FontManager::lineHeight(sBodyFont) - 6.0f,
                         TEXT_DIM);
    UIRenderer::drawRect(fx, fy, field_w, field_h, FIELD_BG);
    // Border.
    UIRenderer::drawRect(fx, fy, field_w, 1.5f, FIELD_BORDER);
    UIRenderer::drawRect(fx, fy + field_h - 1.5f, field_w, 1.5f, FIELD_BORDER);
    UIRenderer::drawRect(fx, fy, 1.5f, field_h, FIELD_BORDER);
    UIRenderer::drawRect(fx + field_w - 1.5f, fy, 1.5f, field_h, FIELD_BORDER);

    const float text_y = fy + (field_h - FontManager::lineHeight(sBodyFont)) * 0.5f;
    float advance = UIRenderer::drawText(sBodyFont, sName, fx + 10.0f, text_y, TEXT_WHITE);

    // Blinking cursor.
    const uint32_t ticks = SDL_GetTicks();
    if ((ticks / 500) % 2 == 0)
    {
        const float cursor_x = fx + 10.0f + advance + 2.0f;
        UIRenderer::drawRect(cursor_x, text_y, 2.0f, FontManager::lineHeight(sBodyFont),
                             CURSOR_COLOR);
    }

    // Buttons.
    int mouseX = 0;
    int mouseY = 0;
    SDL_GetMouseState(&mouseX, &mouseY);
    const float mx = static_cast<float>(mouseX);
    const float my = static_cast<float>(mouseY);

    const float btn_pad_x = 30.0f;
    const float btn_pad_y = 10.0f;
    const float btn_gap = 20.0f;
    const float line_h = FontManager::lineHeight(sTitleFont);
    const float btn_h = line_h + btn_pad_y * 2.0f;

    const char* labels[2] = {"Start", "Back"};
    float by = fy + field_h + 40.0f;

    for (int i = 0; i < 2; ++i)
    {
        const bool disabled = (i == 0 && !isNameValid());
        TextSize sz = UIRenderer::measureText(sTitleFont, labels[i]);
        const float bw = sz.width + btn_pad_x * 2.0f;
        const float bx = (ww - bw) * 0.5f;

        const bool hovered = !disabled && (mx >= bx && mx < bx + bw && my >= by && my < by + btn_h);
        if (hovered)
            sSel = i;

        const bool selected = (i == sSel);
        UIRenderer::drawRect(bx, by, bw, btn_h, (selected && !disabled) ? BTN_BG_HL : BTN_BG);
        UIRenderer::drawText(sTitleFont, labels[i], bx + btn_pad_x, by + btn_pad_y,
                             disabled ? BTN_DIM : (selected ? BTN_HOVER : BTN_NORMAL));

        if (hovered && mouseClicked(em))
        {
            if (i == 0 && isNameValid())
            {
                result = Action::Start;
                SDL_StopTextInput();
            }
            else if (i == 1)
            {
                result = Action::Back;
                SDL_StopTextInput();
            }
            if (result != Action::None && !snd.ui_click.path.empty())
                AudioSystem::playSfx(snd.ui_click.path, snd.ui_click.volume);
        }

        by += btn_h + btn_gap;
    }

    return result;
}
