#include "Hud.h"

#include "Aim.h"
#include "Combat.h"
#include "Engine.h"
#include "Player.h"
#include "ScreenStyle.h"
#include "Swarm.h"
#include "Tools.h"
#include "UIRenderer.h"
#include "ecs/Components.h"
#include "ecs/EntityManager.h"

#include <SDL.h>

#include <algorithm>
#include <cmath>
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

// What is left of him. Above the stamina bar because it is the one you must never have to hunt
// for, and red because nothing else on this screen is.
void healthBar(const EntityManager& em, float x, float y)
{
    const entt::entity p = player::entity();
    const auto& reg = em.registry();
    if (!reg.valid(p) || !reg.all_of<Health>(p))
        return;
    const auto& hp = reg.get<Health>(p);
    if (hp.max <= 0)
        return;
    const float frac = std::max(0.0f, static_cast<float>(hp.current) / static_cast<float>(hp.max));
    UIRenderer::drawRect(x - 1.0f, y - 1.0f, kBarW + 2.0f, kBarH + 2.0f, Color{0, 0, 0, 0.7f});
    UIRenderer::drawRect(x, y, kBarW * frac, kBarH, Color{0.75f, 0.2f, 0.18f, 0.95f});
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

// What the floor is doing, top-centre where the eye goes when things change. A wave count is the
// difference between "endless" and "nearly through it", which is the whole reason waves exist
// rather than one continuous stream; the breath between them is announced loudly because it is
// the only moment the player gets to breathe too.
void waveState(Engine& engine, const EntityManager& em)
{
    const auto cx = static_cast<float>(engine.windowWidth()) * 0.5f;
    const swarm::Phase p = swarm::phase();
    if (p == swarm::Phase::Cleared)
    {
        screen_style::headingCentered("CLEAR", cx, kMargin * 2.0f, screen_style::kTextHot);
        return;
    }
    if (p == swarm::Phase::Breath)
    {
        screen_style::headingCentered("wave " + std::to_string(swarm::waveNumber() + 1) +
                                          " incoming",
                                      cx, kMargin * 2.0f, screen_style::kAccent);
        return;
    }
    screen_style::textCentered("wave " + std::to_string(swarm::waveNumber()) + " of " +
                                   std::to_string(swarm::totalWaves()) + "   " +
                                   std::to_string(swarm::remaining(em)) + " left",
                               cx, kMargin * 2.0f, screen_style::kText);
}

// NOTE: hit areas are NOT drawn. The hitbox and the effect are separate objects on purpose --
// the hitbox is shaped for fairness, the effect for feel -- and drawing the hitbox as well as the
// effect shows the player two things where there is one attack. The spray IS the picture of the
// wand's cone; a wedge underneath it only competes. See Tools.cpp (spawnDroplet) for the visual
// and HitArea.h for the rule it stands in for.

// WHAT IS IN HIS HAND, and how much is left in it. Bottom-right, opposite the body meters on the
// left: what he IS goes on one side, what he is HOLDING on the other, so a glance at either does
// not have to parse the other.
//
// The tank is a number as well as a bar. A bar answers "roughly how much"; mid-swarm the question
// is "can I hold this trigger for another two seconds", and only a number answers that.
void equipped(Engine& engine, const EntityManager& em)
{
    const auto& kit = tools::all();
    if (kit.empty())
        return;
    const auto& held = kit[static_cast<size_t>(tools::selected())];

    const auto right = static_cast<float>(engine.windowWidth()) - kMargin;
    const auto bottom = static_cast<float>(engine.windowHeight());
    const float nameY = bottom - kMargin - kBarH - 30.0f;

    const auto& reg = em.registry();
    const entt::entity p = player::entity();
    float cur = 0.0f;
    float max = 0.0f;
    if (reg.valid(p) && reg.all_of<Charge>(p))
    {
        cur = reg.get<Charge>(p).current;
        max = reg.get<Charge>(p).max_charge;
    }

    const std::string ammo =
        std::to_string(static_cast<int>(cur)) + " / " + std::to_string(static_cast<int>(max));
    const float ammoW = UIRenderer::measureText(screen_style::bodyFont(), ammo).width;
    const float nameW = UIRenderer::measureText(screen_style::bodyFont(), held.name).width;

    // Amber under a quarter: the tank running low is the thing that decides whether to keep
    // spraying or go earn some back, and it must be noticeable without being read.
    const bool low = max > 0.0f && cur / max < 0.25f;
    screen_style::text(held.name, right - nameW, nameY, screen_style::kTextDim);
    screen_style::text(ammo, right - ammoW, nameY + 20.0f,
                       low ? screen_style::kAccent : screen_style::kTextHot);

    if (kit.size() > 1)
        screen_style::text("Q", right - nameW - 22.0f, nameY, screen_style::kTextDim);
}

// A sliver over each hurt creature. Shown only once something has been hit: a swarm of full bars
// is visual noise over enemies that die in two hits anyway, where a bar on the WOUNDED ones tells
// you which to finish.
void enemyBars(Engine& engine, const EntityManager& em, float camX, float camY, int zoom)
{
    const auto z = static_cast<float>(zoom);
    const float halfW = static_cast<float>(engine.windowWidth()) / (2.0f * z);
    const float halfH = static_cast<float>(engine.windowHeight()) / (2.0f * z);
    constexpr float kW = 14.0f;
    constexpr float kH = 3.0f;

    const auto& reg = em.registry();
    for (auto [entity, t, hp, vermin] : reg.view<Transform, Health, Vermin>().each())
    {
        if (hp.max <= 0 || hp.current >= hp.max || reg.all_of<Dying>(entity))
            continue;
        const float frac =
            std::max(0.0f, static_cast<float>(hp.current) / static_cast<float>(hp.max));
        const float sx = (t.x - (camX - halfW)) * z - kW * 0.5f;
        const float sy = (t.y - 8.0f - (camY - halfH)) * z;
        UIRenderer::drawRect(sx - 1.0f, sy - 1.0f, kW + 2.0f, kH + 2.0f, Color{0, 0, 0, 0.75f});
        UIRenderer::drawRect(sx, sy, kW * frac, kH, Color{0.8f, 0.25f, 0.2f, 0.95f});
    }
}

} // namespace

void renderWorldOverlays(Engine& engine, EntityManager& em, float camX, float camY, int zoom)
{
    enemyBars(engine, em, camX, camY, zoom);
}

void render(Engine& engine, EntityManager& em)
{
    int mx = 0;
    int my = 0;
    SDL_GetMouseState(&mx, &my);
    reticle(mx, my);

    const auto bottom = static_cast<float>(engine.windowHeight());
    staminaBar(em, kMargin, bottom - kMargin - kBarH);
    healthBar(em, kMargin, bottom - kMargin - kBarH * 2.0f - 4.0f);

    waveState(engine, em);

    equipped(engine, em);
}

void cursorForPhase(bool playing)
{
    // Derived from the state every frame rather than toggled at each transition: there are
    // several ways into and out of a menu, and one of them will eventually forget.
    SDL_ShowCursor(playing ? SDL_DISABLE : SDL_ENABLE);
}

} // namespace hud
