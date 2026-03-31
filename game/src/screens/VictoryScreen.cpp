#include "screens/VictoryScreen.h"

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
static float sTimer = 0.0f;

static constexpr float FADE_DURATION = 1.0f;
static constexpr float PROMPT_DELAY = 2.0f;
static constexpr Color GOLD{0.95f, 0.85f, 0.4f, 1.0f};
static constexpr Color TEXT_DIM{0.5f, 0.48f, 0.46f, 1.0f};

void VictoryScreen::init(FontHandle body_font, FontHandle title_font, FontHandle big_title_font)
{
    sBodyFont = body_font;
    sTitleFont = title_font;
    sBigTitleFont = big_title_font;
}

void VictoryScreen::reset()
{
    sTimer = 0.0f;
}

bool VictoryScreen::render(EntityManager& em, int window_w, int window_h, float dt)
{
    ZoneScopedN("VictoryScreen");

    sTimer += dt;

    const float ww = static_cast<float>(window_w);
    const float wh = static_cast<float>(window_h);

    const float alpha = std::min(1.0f, sTimer / FADE_DURATION) * 0.85f;
    UIRenderer::drawRect(0.0f, 0.0f, ww, wh, {0.0f, 0.0f, 0.0f, alpha});

    const std::string title = "YOU ESCAPED";
    const TextSize tsz = UIRenderer::measureText(sBigTitleFont, title);
    const float textAlpha = std::min(1.0f, sTimer / FADE_DURATION);
    UIRenderer::drawText(sBigTitleFont, title, (ww - tsz.width) * 0.5f, wh * 0.35f,
                         {GOLD.r, GOLD.g, GOLD.b, textAlpha});

    if (sTimer >= PROMPT_DELAY)
    {
        const std::string prompt = "Press any key to continue";
        const TextSize psz = UIRenderer::measureText(sTitleFont, prompt);
        const float blink = ((SDL_GetTicks() / 600) % 2 == 0) ? 1.0f : 0.5f;
        UIRenderer::drawText(sTitleFont, prompt, (ww - psz.width) * 0.5f, wh * 0.55f,
                             {TEXT_DIM.r, TEXT_DIM.g, TEXT_DIM.b, blink});

        if (!em.key_down_events.empty() || !em.mouse_down_events.empty())
        {
            const auto& snd = em.registry().ctx().get<SoundConfig>();
            if (!snd.ui_click.path.empty())
                AudioSystem::playSfx(snd.ui_click.path, snd.ui_click.volume);
            return true;
        }
    }

    return false;
}
