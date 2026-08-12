#include "renderers/HudRenderer.h"

#include "Engine.h"
#include "UIRenderer.h"
#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "ecs/GameComponents.h"
#include "ops/NavUtils.h"
#include "ops/ZoneUtils.h"
#include "renderers/DebugPanelRenderer.h"
#include "renderers/NotificationRenderer.h"
#include "renderers/PromptRenderer.h"
#include "screens/ScreenStyle.h"
#include "systems/AimSystem.h"
#include "systems/CombatSystem.h"
#include "systems/PlayerSystem.h"
#include "systems/RewardSystem.h"
#include "systems/ThermosSystem.h"
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

// Shell metrics in UNITS, resolved through the ladder -- the bars and margins grow with the
// type instead of drifting away from it at higher scales.
float barW()
{
    return screen_style::pad(25);
}
float barH()
{
    return screen_style::pad(1) + 2.0f;
}
float margin()
{
    return screen_style::pad(4);
}

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

// WHO OWNS THE POINTER this frame -- set when the system cursor is taken away, read when the
// crosshair is drawn, so the two can never both be on screen or both be off it.
bool sCrosshair = false;

// A cross rather than a filled shape: the thing under the cursor is what you are about to hit,
// and a solid reticle hides it.
void reticle(int mouseX, int mouseY)
{
    const auto x = static_cast<float>(mouseX);
    const auto y = static_cast<float>(mouseY);
    constexpr float kArm = 7.0f;
    constexpr float kGap = 3.0f;
    constexpr float kThick = 2.0f;
    constexpr Color c = screen_style::kReticle;
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
    UIRenderer::drawRect(x - 1.0f, y - 1.0f, barW() + 2.0f, barH() + 2.0f, screen_style::kBarBack);
    UIRenderer::drawRect(x, y, barW() * frac, barH(), screen_style::kHealth);
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
    UIRenderer::drawRect(x - 1.0f, y - 1.0f, barW() + 2.0f, barH() + 2.0f, screen_style::kBarBack);
    // Amber while spending, green once it is recovering -- the colour says WHY a shot did not
    // come out, which an empty bar alone does not.
    const bool spent = sta.recovery_timer > 0.0f;
    const Color fill = spent ? screen_style::kStaminaSpent : screen_style::kStaminaFresh;
    UIRenderer::drawRect(x, y, barW() * frac, barH(), fill);
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

    // The tool's own box, the pocket's sibling: name row over ammo row.
    const float boxW = screen_style::pad(24);
    const float rowH = screen_style::lineHeight() + screen_style::pad(1);
    const float boxH = rowH * 2.0f;
    const float bx = margin();
    const float by = bottom - margin() - boxH;
    screen_style::panel(screen_style::Rect{bx, by, boxW, boxH});

    // STOWED outside a combat zone: the whole box goes quiet -- the kit is
    // carried here, not drawn. Amber under a quarter otherwise: the tank
    // running low decides whether to keep spraying or go earn some back.
    const bool live = zone::combat();
    const bool low = max > 0.0f && cur / max < 0.25f;
    // The kit fades between carried and drawn on the changeover rather than
    // snapping, so the eye is handed the moment the trade starts or stops.
    constexpr float kStowed = 0.45f;
    const float lit = live ? zone::settle() : 1.0f - zone::settle();
    const float strength = kStowed + (1.0f - kStowed) * lit;
    const Color nameCol = screen_style::withAlpha(screen_style::kTextDim, strength);
    const Color hot = low ? screen_style::kAccent : screen_style::kTextHot;
    const Color ammoCol = screen_style::withAlpha(hot, hot.a * strength);
    screen_style::textInBox(held.name, screen_style::Rect{bx, by, boxW, rowH}, nameCol,
                            /*alignRight=*/false);
    screen_style::textInBox(ammo, screen_style::Rect{bx, by + rowH, boxW, rowH}, ammoCol,
                            /*alignRight=*/false);
    if (kit.size() > 1)
        screen_style::textInBox("Q", screen_style::Rect{bx, by, boxW - screen_style::pad(1), rowH},
                                nameCol, /*alignRight=*/true);
}

// A sliver over each hurt creature. Shown only once something has been hit: a swarm of full bars
// is visual noise over enemies that die in two hits anyway, where a bar on the WOUNDED ones tells
// you which to finish.
// DEBUG: the hit areas as they actually are -- the cone's edges, its full
// reach, and (brighter) how far the damaging front has swept. What the F1
// toggle shows is the exact geometry HitDetection tests, so a "the spray
// touched it but nothing died" moment can be read instead of guessed at.
void hitAreaOverlay(Engine& engine, const EntityManager& em, float camX, float camY, int zoom)
{
    if (!debug_panel::showHitAreas())
        return;
    const auto z = static_cast<float>(zoom);
    const float halfW = static_cast<float>(engine.windowWidth()) / (2.0f * z);
    const float halfH = static_cast<float>(engine.windowHeight()) / (2.0f * z);
    const auto plot = [&](float wx, float wy, const Color& c)
    {
        UIRenderer::drawRect((wx - (camX - halfW)) * z - 1.0f, (wy - (camY - halfH)) * z - 1.0f,
                             2.0f, 2.0f, c);
    };

    for (const auto [entity, t, area] : em.registry().view<Transform, HitArea>().each())
    {
        const float front =
            area.expand > 0.0f ? std::min(area.radius, area.expand * area.age) : area.radius;
        const float aim = std::atan2(area.dir_y, area.dir_x);
        const float half = area.arc > 0.0f ? geom::degToRad(area.arc) : geom::kPi;
        // The cone's two edges, out to full reach.
        for (float d = 4.0f; d <= area.radius; d += 4.0f)
        {
            plot(t.x + std::cos(aim - half) * d, t.y + std::sin(aim - half) * d,
                 screen_style::kReticle);
            plot(t.x + std::cos(aim + half) * d, t.y + std::sin(aim + half) * d,
                 screen_style::kReticle);
        }
        // Full reach rim (faint) and the swept front (hot).
        for (float a = -half; a <= half; a += 0.12f)
        {
            plot(t.x + std::cos(aim + a) * area.radius, t.y + std::sin(aim + a) * area.radius,
                 screen_style::kReticle);
            plot(t.x + std::cos(aim + a) * front, t.y + std::sin(aim + a) * front,
                 screen_style::kDamage);
        }
    }
}

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
        UIRenderer::drawRect(sx - 1.0f, sy - 1.0f, kW + 2.0f, kH + 2.0f, screen_style::kBarBack);
        UIRenderer::drawRect(sx, sy, kW * frac, kH, screen_style::kHealth);
    }
}

} // namespace

// The display's memory of the pocket belongs to ONE SITTING. A job put back from disk arrives
// with its total already earned, and a pump that remembered the last total it drew would read
// the whole difference as income and animate it in -- money he earned yesterday, pouring in
// again because the renderer had no way to know a world had been torn down under it.
void reset()
{
    sLastBanked = -1;
    sPending = 0;
    sHold = 0.0f;
    sPumpCarry = 0.0f;
}

void renderWorldOverlays(Engine& engine, EntityManager& em, float camX, float camY, int zoom)
{
    enemyBars(engine, em, camX, camY, zoom);
    hitAreaOverlay(engine, em, camX, camY, zoom);
}

void render(Engine& engine, EntityManager& em)
{
    // Drawn on exactly the frames the HUD took the pointer away from the desktop, from the one
    // value that decided it. The pointer has ONE owner: a crosshair easing in over a system
    // cursor that is still there shows two, which is the one thing this may never do.
    if (sCrosshair)
    {
        int mx = 0;
        int my = 0;
        SDL_GetMouseState(&mx, &my);
        reticle(mx, my);
    }

    // Frame time for the purely-visual pump; clamped so a hitch cannot teleport the animation.
    static Uint64 sLastTicks = 0;
    const Uint64 now = SDL_GetTicks64();
    const float dt =
        sLastTicks == 0 ? 0.0f : std::min(0.1f, static_cast<float>(now - sLastTicks) / 1000.0f);
    sLastTicks = now;

    // TOP LEFT: the body. Health over stamina, the reading order of every game in this shape;
    // the thermos count under them -- the flask lives with the body it mends.
    healthBar(em, margin(), margin());
    staminaBar(em, margin(), margin() + barH() + 6.0f);
    {
        const auto& fill = thermos::fills();
        const std::string label = fill.empty()
                                      ? std::string{"thermos"}
                                      : fill[static_cast<std::size_t>(thermos::fillIndex())].name;
        screen_style::text(label + "  " + std::to_string(thermos::sipsLeft()) + "/" +
                               std::to_string(thermos::sipsMax()) + "  (R)",
                           margin(), margin() + barH() * 2.0f + 14.0f,
                           thermos::sipsLeft() > 0 ? screen_style::kText : screen_style::kAccent);
    }

    // BOTTOM LEFT: what is in his hand, and the acquisition feed above it.
    equipped(engine, em);
    notify::render(engine, dt);
    prompt::render(engine);

    // BOTTOM RIGHT: the pocket, in a box, with the gain pumping into it.
    tickPump(reward::banked(em), dt);
    const auto right = static_cast<float>(engine.windowWidth()) - margin();
    const auto bottom = static_cast<float>(engine.windowHeight()) - margin();
    const float boxW = screen_style::pad(19);
    const float boxH = screen_style::lineHeight() + screen_style::pad(2);
    const float bx = right - boxW;
    const float by = bottom - boxH;
    screen_style::panel(screen_style::Rect{bx, by, boxW, boxH});
    screen_style::textInBox(std::to_string(reward::banked(em) - sPending),
                            screen_style::Rect{bx, by, boxW, boxH}, screen_style::kText,
                            /*alignRight=*/true);

    // THE RATE, over the box, whenever holding more than one front is paying more than one
    // front's worth. Shown while he is deciding rather than in the receipt afterwards: a
    // multiplier he only learns about at the staging area is not a reason to take a risk.
    if (const float rate = reward::rate(); rate > 1.0f)
    {
        const int tenths = static_cast<int>(std::lround(rate * 10.0f));
        screen_style::text("x" + std::to_string(tenths / 10) + "." + std::to_string(tenths % 10),
                           bx + screen_style::pad(2),
                           by - screen_style::lineHeight() - screen_style::pad(1),
                           screen_style::kStaminaSpent);
    }

    // GREEN, not the accent red: this is income, and red is the game's alarm colour -- a gain
    // painted like a warning reads as something being taken.
    if (sPending > 0)
        screen_style::textRight("+" + std::to_string(sPending), bx + boxW - screen_style::pad(2),
                                by - screen_style::lineHeight() - screen_style::pad(1),
                                screen_style::kGain);
}

void cursorForPhase(bool crosshair)
{
    // Derived from the state every frame rather than toggled at each transition: there are
    // several ways into and out of a menu, and one of them will eventually forget.
    sCrosshair = crosshair;
    SDL_ShowCursor(crosshair ? SDL_DISABLE : SDL_ENABLE);
}

} // namespace hud
