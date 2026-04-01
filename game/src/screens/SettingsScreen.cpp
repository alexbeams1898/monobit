#include "screens/SettingsScreen.h"

#include "UIRenderer.h"
#include "ecs/EntityManager.h"
#include "ecs/GameConfig.h"
#include "screens/MainMenuScreen.h"
#include "screens/MenuDialog.h"
#include "screens/ScreenColors.h"
#include "screens/ScreenInput.h"
#include "systems/AudioSystem.h"

#include <string>
#include <tracy/Tracy.hpp>
#include <vector>

static FontHandle sBodyFont = INVALID_FONT;
static FontHandle sTitleFont = INVALID_FONT;
static int sSel = 0;

void SettingsScreen::init(FontHandle body_font, FontHandle title_font)
{
    sBodyFont = body_font;
    sTitleFont = title_font;
}

void SettingsScreen::reset()
{
    sSel = 0;
}

void SettingsScreen::render(EntityManager& em, int window_w, int window_h)
{
    ZoneScopedN("SettingsScreen");

    const float ww = static_cast<float>(window_w);
    const float wh = static_cast<float>(window_h);
    auto& dbg = em.registry().ctx().get<DebugFlags>();

    std::vector<MenuDialog::Option> options;
    options.push_back({dbg.god_mode ? "God Mode: ON" : "God Mode: OFF",
                       "Invincibility and infinite stamina", true});
    options.push_back({"Back", "Return to main menu", true});

    // Full-screen opaque background (matches MainMenu) to prevent flash on transition.
    UIRenderer::drawRect(0.0f, 0.0f, ww, wh, screen_colors::OVERLAY_OPAQUE);

    MenuDialog::Options dlgOpts;
    dlgOpts.title_font = sTitleFont;
    dlgOpts.body_font = sBodyFont;
    dlgOpts.title = "Settings";
    dlgOpts.items = options;
    dlgOpts.hint = "[Enter/Click] Toggle   [Esc/RMB] Back";
    dlgOpts.selection = &sSel;
    dlgOpts.darken_background = false;

    auto result = MenuDialog::render(em, dlgOpts, ww, wh);

    if (result.dismissed)
    {
        screen_input::playClickSfx(em);
        MainMenuScreen::reset();
        em.registry().ctx().get<GameState>().phase = GameState::Phase::MainMenu;
        return;
    }

    if (result.selected >= 0)
    {
        if (result.selected == 0)
        {
            dbg.god_mode = !dbg.god_mode;
            screen_input::playClickSfx(em);
        }
        else if (result.selected == 1)
        {
            MainMenuScreen::reset();
            em.registry().ctx().get<GameState>().phase = GameState::Phase::MainMenu;
        }
    }
}
