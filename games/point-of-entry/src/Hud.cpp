#include "Hud.h"

#include "Aim.h"
#include "Combat.h"
#include "Engine.h"
#include "Player.h"
#include "ScreenStyle.h"
#include "Tools.h"
#include "UIRenderer.h"
#include "ecs/EntityManager.h"

#include <SDL.h>

#include <string>

#include <entt/entt.hpp>

namespace hud
{
namespace
{

constexpr float kBarW = 180.0f;
constexpr float kBarH = 10.0f;
constexpr float kMargin = 14.0f;

// A cross rather than a filled shape: the thing under the cursor is what you are about to hit,
// and a solid reticle hides it.
void reticle(int mouseX, int mouseY)
{
    const auto x = static_cast<float>(mouseX);
    const auto y = static_cast<float>(mouseY);
    constexpr float kArm = 7.0f;
    constexpr float kGap = 3.0f;
    constexpr float kThick = 2.0f;
    constexpr Color c{0.95f, 0.95f, 0.9f, 0.9f};
    UIRenderer::drawRect(x - kGap - kArm, y - kThick * 0.5f, kArm, kThick, c);
    UIRenderer::drawRect(x + kGap, y - kThick * 0.5f, kArm, kThick, c);
    UIRenderer::drawRect(x - kThick * 0.5f, y - kGap - kArm, kThick, kArm, c);
    UIRenderer::drawRect(x - kThick * 0.5f, y + kGap, kThick, kArm, c);
}

// The bar goes red while it is refusing to fire, so an empty bar explains itself rather than
// looking like a broken button.
void staminaBar(const EntityManager& em, float x, float y)
{
    const entt::entity p = player::entity();
    const auto& reg = em.registry();
    if (!reg.valid(p) || !reg.all_of<Stamina>(p))
        return;
    const auto& sta = reg.get<Stamina>(p);
    if (sta.max_stamina <= 0.0f)
        return;

    const float frac = sta.current / sta.max_stamina;
    UIRenderer::drawRect(x - 1.0f, y - 1.0f, kBarW + 2.0f, kBarH + 2.0f, Color{0, 0, 0, 0.7f});
    // Amber while spending, green once it is recovering -- the colour says WHY a shot did not
    // come out, which an empty bar alone does not.
    const bool spent = sta.recovery_timer > 0.0f;
    const Color fill = spent ? Color{0.85f, 0.6f, 0.25f, 0.95f} : Color{0.45f, 0.8f, 0.35f, 0.95f};
    UIRenderer::drawRect(x, y, kBarW * frac, kBarH, fill);
}

} // namespace

void render(Engine& engine, EntityManager& em)
{
    int mx = 0;
    int my = 0;
    SDL_GetMouseState(&mx, &my);
    reticle(mx, my);

    const auto bottom = static_cast<float>(engine.windowHeight());
    staminaBar(em, kMargin, bottom - kMargin - kBarH);

    // Which tool is in hand, and what else he is carrying. The held one is bright; the rest are
    // there so the number keys mean something before there is an inventory screen.
    const auto& kit = tools::all();
    float ty = bottom - kMargin - kBarH - 26.0f;
    for (int i = static_cast<int>(kit.size()) - 1; i >= 0; --i)
    {
        const bool held = i == tools::selected();
        const Color c = held ? screen_style::kTextHot : screen_style::kTextDim;
        screen_style::text(std::to_string(i + 1) + "  " + kit[static_cast<size_t>(i)].name, kMargin,
                           ty, c);
        ty -= 18.0f;
    }
}

void cursorForPhase(bool playing)
{
    // Derived from the state every frame rather than toggled at each transition: there are
    // several ways into and out of a menu, and one of them will eventually forget.
    SDL_ShowCursor(playing ? SDL_DISABLE : SDL_ENABLE);
}

} // namespace hud
