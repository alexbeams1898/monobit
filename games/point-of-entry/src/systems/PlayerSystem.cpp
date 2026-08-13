#include "systems/PlayerSystem.h"

#include "ecs/BalanceConfig.h"
#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "ops/CaptureUtils.h"
#include "ops/LogUtils.h"
#include "ops/NavUtils.h"
#include "renderers/DebugPanelRenderer.h"
#include "systems/AimSystem.h"
#include "systems/CameraSystem.h"
#include "systems/CombatSystem.h"
#include "systems/RewardSystem.h"
#include "systems/SpriteAnimSystem.h"
#include "systems/ThermosSystem.h"

#include <SDL.h>

#include <algorithm>
#include <cmath>

namespace player
{
namespace
{
// Walk speed lives on the dev panel (F1), not here: how fast a man should walk is a thing
// you judge by walking, and a slider settles it in a minute where a rebuild-and-relaunch
// loop takes twenty. It is stepped in WHOLE PIXELS PER TICK there, which matters -- the
// camera rounds to an internal pixel every frame, so a speed that does not divide evenly
// makes the world scroll in uneven steps and reads as lag while nothing is late.

entt::entity sPlayer = entt::null;
EntityManager* sEm = nullptr;  // for the key handlers; set every update
bool sInteractPressed = false; // an unconsumed E edge

// What he pressed this tick, for consumers that care about intent rather
// than achieved movement (doors against walls).
float sIntentX = 0.0f;
float sIntentY = 0.0f;

// ONE DRAWING, FLIPPED -- the whole game's sprite convention. A character is drawn once and
// mirrored to face the other way; there is no back sprite and no up/down pose. It is what
// makes a bestiary running from ants to demons affordable: every new creature is one
// drawing, not four.
//
// EVERY CHARACTER IS DRAWN FACING RIGHT, which is the direction needing no correction anywhere
// -- art facing left would need its sign inverted for that one sprite, and that is a rule
// nobody remembers on the fortieth creature. Source drawn the wrong way is corrected in the
// .aseprite file itself, never compensated for in code.
//
// Sub-pixel movement not yet applied, carried between ticks (see stepWhole).
float sCarryX = 0.0f;
float sCarryY = 0.0f;

// WASD / arrows -> a direction. Diagonals are not normalised: this is the
// skeleton, and the feel pass comes with the real movement system.
void readMoveDir(const Uint8* keys, float& dx, float& dy)
{
    if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT])
        dx -= 1.0f;
    if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT])
        dx += 1.0f;
    if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP])
        dy -= 1.0f;
    if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN])
        dy += 1.0f;

    // NORMALISE, or holding two keys moves the full step on BOTH axes -- 1.41x the speed of
    // walking straight, which is both a gameplay bug and the reason diagonal movement looked
    // rougher than cardinal.
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len > 0.0f)
    {
        dx /= len;
        dy /= len;
    }
}

// Dev hotkeys. Edge-triggered, so holding a key fires once rather than every frame.
//   F1  the dev panel
//   F12 write frame.png beside the exe
void pollDevKeys(const Uint8* keys)
{
    static bool sPrevPanel = false;
    static bool sPrevShot = false;
    const bool panel = keys[SDL_SCANCODE_F1] != 0;
    const bool shot = keys[SDL_SCANCODE_F12] != 0;
    if (panel && !sPrevPanel)
        debug_panel::toggle();
    if (shot && !sPrevShot)
        capture::request();
    sPrevPanel = panel;
    sPrevShot = shot;

    // Q cycles the kit. One key rather than a row of numbered slots: he carries a few tools, not
    // a hotbar, and a number per tool stops meaning anything the moment the kit is bigger than
    // the hand can reach.
    static bool sPrevCycle = false;
    const bool cycle = keys[SDL_SCANCODE_Q] != 0;
    if (cycle && !sPrevCycle)
        tools::next();
    sPrevCycle = cycle;

    // R drinks; E rests at the staging area. Edge-triggered like everything else here.
    static bool sPrevSip = false;
    const bool sipKey = keys[SDL_SCANCODE_R] != 0;
    if (sipKey && !sPrevSip)
        thermos::sip(*sEm);
    sPrevSip = sipKey;

    // SPACE is the interact key -- the thumb's key, always under the hand. The edge is RECORDED
    // here and consumed by the shell, which knows what is in reach and what interacting means;
    // this file only knows a key went down.
    static bool sPrevInteract = false;
    const bool interactKey = keys[SDL_SCANCODE_SPACE] != 0;
    if (interactKey && !sPrevInteract)
        sInteractPressed = true;
    sPrevInteract = interactKey;
}

// Move `pos` by `amount`, but only ever in WHOLE pixels -- the fraction is carried in
// `carry` and applied once it adds up to one.
//
// This is what lets a normalised diagonal work. A normalised step is 1.414 px/tick, and
// rounding that every frame gives 1,2,1,2,1,1,2 -- the uneven scroll that reads as lag.
// Carrying the remainder instead means the position is always an integer AND the average
// speed is exactly right; the extra pixel simply lands on the tick where the debt comes due.
// The movement test box: the player's collider, shrunk by an inset on each side. The inset is
// what stops corner-sticking -- approaching a doorway at a slight angle, the full-size box
// clips the adjacent wall corner by a pixel and that axis dies even though he is clearly
// sliding past. Two pixels of forgiveness absorbs the clip; a 2px-smaller box still cannot
// pass through a full tile of wall.
constexpr float kMoveInset = 2.0f;

void stepWhole(float& pos, float& carry, float amount, const EntityManager& em, bool horizontal,
               float otherAxis, float boxW, float boxH)
{
    carry += amount;
    const float whole = std::trunc(carry);
    if (whole == 0.0f)
        return;
    carry -= whole;
    if (!world::stepBlocked(em, pos, whole, horizontal, otherAxis, boxW, boxH))
        carry = 0.0f; // hit a wall -- drop the debt rather than paying it into the wall
}

} // namespace

void bind(entt::entity player)
{
    sPlayer = player;
}

entt::entity entity()
{
    return sPlayer;
}

void standAt(EntityManager& em, float x, float y)
{
    auto& reg = em.registry();
    const entt::entity p = entity();
    if (!reg.valid(p))
        return;
    auto& at = reg.get<Transform>(p);
    at.x = x;
    at.y = y;
    reg.get<PreviousTransform>(p) = PreviousTransform{x, y};
    if (auto* cam = reg.try_get<Camera>(p))
    {
        cam->x = x;
        cam->y = y;
        cam->prev_x = x;
        cam->prev_y = y;
    }
}

bool consumeInteract()
{
    const bool was = sInteractPressed;
    sInteractPressed = false;
    return was;
}

void moveIntent(float& dx, float& dy)
{
    dx = sIntentX;
    dy = sIntentY;
}

void update(Engine& /*engine*/, EntityManager& em, double dt)
{
    if (!em.registry().valid(sPlayer))
        return;

    sEm = &em;
    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    pollDevKeys(keys);

    float dx = 0.0f;
    float dy = 0.0f;
    readMoveDir(keys, dx, dy);
    sIntentX = dx;
    sIntentY = dy;

    auto& t = em.registry().get<Transform>(sPlayer);
    // WHERE HE FACES. Spraying, he faces his work -- the aim side wins for as long as the
    // stream is held, and near-vertical aim keeps whichever side he already had rather than
    // flickering at the boundary. The moment the trigger releases, facing snaps back to
    // movement. Through FacingDirection rather than onto the sprite directly: the sprite's
    // flip is OWNED by AnimationSystem, which rewrites it from facing every tick.
    auto& facing = em.registry().get_or_emplace<FacingDirection>(sPlayer);
    if (tools::streaming(em))
    {
        if (std::abs(aim::dirX()) > 0.1f)
        {
            facing.dx = aim::dirX() < 0.0f ? -1.0f : 1.0f;
            facing.render_dx = facing.dx;
        }
    }
    else if (dx != 0.0f)
    {
        facing.dx = dx < 0.0f ? -1.0f : 1.0f;
        facing.render_dx = facing.dx;
    }

    // THE WALK IS NOT AN ANIMATION. A character is one drawing, and the gait is the code-driven
    // hop and sway in WalkBob -- so there is no "walk" tag to play here and drawn walk frames
    // would fight it, each bobbing the body by a different rule.
    //
    // Tags are still the mechanism for poses a drawing cannot express by moving: a swing, a
    // flinch, a death. Those get played from wherever they happen.
    sprite_anim::playIfPresent(em, sPlayer, "idle", "idle");

    // HOLDING THE GUARD UP PLANTS HIM. The gait stiffens -- the sway goes, most of the hop goes
    // -- and his speed does not change: slowing a man for raising his guard punishes the
    // defensive option twice, once in tempo and once in reach.
    if (auto* gait = em.registry().try_get<Gait>(sPlayer))
    {
        // Eased rather than flipped: a man sets himself over a moment, and snapping the gait
        // between two shapes on a keypress reads as a glitch however right the two shapes are.
        constexpr float kBraceTime = 0.18f;
        const float want = aim::guarding() ? 1.0f : 0.0f;
        const float move = static_cast<float>(dt) / kBraceTime;
        gait->braced += std::clamp(want - gait->braced, -move, move);
    }

    if (dx != 0.0f || dy != 0.0f)
    {
        // Braced walks a shade slower AND stiffer -- the stiffness carries the reading, the
        // speed only underlines it.
        const float braced = aim::guarding() ? stats::formulas().block.walk_factor : 1.0f;
        const float step = debug_panel::walkSpeed() * braced * static_cast<float>(dt);
        // The body is a box at his FEET, not a point at his middle: the transform sits in the
        // foot box and the sprite is drawn with its bottom on it (the renderer aligns sprite to
        // collider), so the torso may overlap a wall ABOVE him -- top-down depth -- but his feet
        // never enter one.
        const auto* col = em.registry().try_get<Collider>(sPlayer);
        const float bw = (col != nullptr ? col->width : 16.0f) - kMoveInset;
        const float bh = (col != nullptr ? col->height : 12.0f) - kMoveInset;
        // FULL-SPEED WALL SLIDE. A normalised diagonal into a wall would creep
        // along it at 70% -- the blocked axis still owns its share of the
        // stride. The wall absorbs that share instead: one axis blocked, the
        // free axis takes the whole stride.
        if (dx != 0.0f && dy != 0.0f)
        {
            const bool xBlocked =
                !world::boxFree(em, t.x + (dx < 0.0f ? -1.0f : 1.0f), t.y, bw, bh);
            const bool yBlocked =
                !world::boxFree(em, t.x, t.y + (dy < 0.0f ? -1.0f : 1.0f), bw, bh);
            if (yBlocked && !xBlocked)
            {
                dx = dx < 0.0f ? -1.0f : 1.0f;
                dy = 0.0f;
            }
            else if (xBlocked && !yBlocked)
            {
                dy = dy < 0.0f ? -1.0f : 1.0f;
                dx = 0.0f;
            }
        }
        // One axis at a time, so walking into a wall at an angle slides along it instead of
        // stopping dead.
        stepWhole(t.x, sCarryX, dx * step, em, /*horizontal=*/true, t.y, bw, bh);
        stepWhole(t.y, sCarryY, dy * step, em, /*horizontal=*/false, t.x, bw, bh);
    }
    else
    {
        // Standing still owes nothing; a stale fraction would make the first step of the next
        // move arrive early.
        sCarryX = 0.0f;
        sCarryY = 0.0f;
    }

    // EVERY tick, not just the moving ones: the engine owns camera-follows-transform,
    // and a camera that only syncs while a key is held starts the game looking at
    // wherever it was default-constructed.
    CameraSystem::update(em);
}

} // namespace player
