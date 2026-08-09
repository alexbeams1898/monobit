#include "renderers/HudRenderer.h"

#include "Engine.h"
#include "UIRenderer.h"
#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "ecs/GameComponents.h"
#include "renderers/NotificationRenderer.h"
#include "screens/ScreenStyle.h"
#include "systems/AimSystem.h"
#include "systems/CombatSystem.h"
#include "systems/PlayerSystem.h"
#include "systems/RewardSystem.h"
#include "systems/WaveSystem.h"

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <string>

#include <entt/entt.hpp>

namespace hud
{
namespace
{

constexpr float kBarW = 200.0f;
constexpr float kBarH = 11.0f;
constexpr float kMargin = 18.0f;

// THE PUMP. Kills do not tick the total directly: each one lands in a "+N" that floats above
// the box and keeps accumulating while the killing continues; once it has been quiet for a
// moment, the gain drains INTO the total, visibly counting it up. Purely presentational --
// the pocket itself was paid the instant each kill landed -- but it is what turns a number
// into income: you watch the work become money.
constexpr float kPumpHold = 1.3f;   // quiet seconds before the gain starts draining
constexpr float kPumpRate = 2.5f;   // fraction of the pending gain drained per second...
constexpr float kPumpFloor = 25.0f; // ...but never slower than this many per second
int sLastBanked = -1;
int sPending = 0;
float sHold = 0.0f;
float sPumpCarry = 0.0f;

// The displayed total lags the pocket by whatever is still pending in the "+N".
void tickPump(int bankedNow, float dt)
{
    if (sLastBanked < 0)
        sLastBanked = bankedNow; // first frame: no history, nothing pending
    const int delta = bankedNow - sLastBanked;
    sLastBanked = bankedNow;
    if (delta > 0)
    {
        sPending += delta;
        sHold = kPumpHold;
    }
    else if (delta < 0)
    {
        // Spending (or a new job) is not income; the display snaps rather than animating.
        sPending = 0;
        sPumpCarry = 0.0f;
        return;
    }
    if (sPending <= 0)
        return;
    sHold -= dt;
    if (sHold > 0.0f)
        return;
    sPumpCarry += dt * std::max(kPumpFloor, static_cast<float>(sPending) * kPumpRate);
    const int whole = static_cast<int>(sPumpCarry);
    if (whole > 0)
    {
        sPumpCarry -= static_cast<float>(whole);
        sPending = std::max(0, sPending - whole);
    }
}

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

// WHAT IS IN HIS HAND, bottom-left under the body meters' column -- the equipment corner, as
// the shape's convention has it, leaving bottom-right to the pocket.
//
// The tank is a number as well as a bar. A bar answers "roughly how much"; mid-swarm the question
// is "can I hold this trigger for another two seconds", and only a number answers that.
void equipped(Engine& engine, const EntityManager& em)
{
    const auto& kit = tools::all();
    if (kit.empty())
        return;
    const auto& held = kit[static_cast<size_t>(tools::selected())];

    const auto bottom = static_cast<float>(engine.windowHeight());
    const float nameY = bottom - kMargin - 42.0f;

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

    // Amber under a quarter: the tank running low is the thing that decides whether to keep
    // spraying or go earn some back, and it must be noticeable without being read.
    const bool low = max > 0.0f && cur / max < 0.25f;
    screen_style::text(held.name, kMargin, nameY, screen_style::kTextDim);
    screen_style::text(ammo, kMargin, nameY + 20.0f,
                       low ? screen_style::kAccent : screen_style::kTextHot);
    if (kit.size() > 1)
        screen_style::text(
            "Q",
            kMargin + UIRenderer::measureText(screen_style::bodyFont(), held.name).width + 10.0f,
            nameY, screen_style::kTextDim);
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

    // Frame time for the purely-visual pump; clamped so a hitch cannot teleport the animation.
    static Uint64 sLastTicks = 0;
    const Uint64 now = SDL_GetTicks64();
    const float dt =
        sLastTicks == 0 ? 0.0f : std::min(0.1f, static_cast<float>(now - sLastTicks) / 1000.0f);
    sLastTicks = now;

    // TOP LEFT: the body. Health over stamina, the reading order of every game in this shape.
    healthBar(em, kMargin, kMargin);
    staminaBar(em, kMargin, kMargin + kBarH + 6.0f);

    waveState(engine, em);

    // BOTTOM LEFT: what is in his hand, and the acquisition feed above it.
    equipped(engine, em);
    notify::render(engine, dt);

    // BOTTOM RIGHT: the pocket, in a box, with the gain pumping into it.
    tickPump(reward::banked(em), dt);
    const auto right = static_cast<float>(engine.windowWidth()) - kMargin;
    const auto bottom = static_cast<float>(engine.windowHeight()) - kMargin;
    constexpr float kBoxW = 150.0f;
    constexpr float kBoxH = 32.0f;
    const float bx = right - kBoxW;
    const float by = bottom - kBoxH;
    // A bordered box: frame first, then the inset field.
    UIRenderer::drawRect(bx - 1.0f, by - 1.0f, kBoxW + 2.0f, kBoxH + 2.0f,
                         Color{0.75f, 0.72f, 0.62f, 0.55f});
    UIRenderer::drawRect(bx, by, kBoxW, kBoxH, Color{0.05f, 0.05f, 0.06f, 0.85f});
    const std::string total = std::to_string(reward::banked(em) - sPending);
    const float totalW = UIRenderer::measureText(screen_style::bodyFont(), total).width;
    screen_style::text(total, bx + kBoxW - 10.0f - totalW, by + kBoxH * 0.5f - 8.0f,
                       screen_style::kText);

    if (sPending > 0)
    {
        const std::string gain = "+" + std::to_string(sPending);
        const float gainW = UIRenderer::measureText(screen_style::bodyFont(), gain).width;
        // GREEN, not the accent red: this is income, and red is the game's alarm colour --
        // a gain painted like a warning reads as something being taken.
        screen_style::text(gain, bx + kBoxW - 10.0f - gainW, by - 22.0f,
                           Color{0.55f, 0.85f, 0.4f, 1.0f});
    }
}

void cursorForPhase(bool playing)
{
    // Derived from the state every frame rather than toggled at each transition: there are
    // several ways into and out of a menu, and one of them will eventually forget.
    SDL_ShowCursor(playing ? SDL_DISABLE : SDL_ENABLE);
}

} // namespace hud
