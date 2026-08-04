#include "Player.h"

#include "Capture.h"
#include "DebugPanel.h"
#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "systems/CameraSystem.h"

#include <SDL.h>

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

// Sub-pixel movement not yet applied, carried between ticks (see stepWhole).
float sCarryX = 0.0f;
float sCarryY = 0.0f;

bool walkableAt(const EntityManager& em, float x, float y);

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
}

// Move `pos` by `amount`, but only ever in WHOLE pixels -- the fraction is carried in
// `carry` and applied once it adds up to one.
//
// This is what lets a normalised diagonal work. A normalised step is 1.414 px/tick, and
// rounding that every frame gives 1,2,1,2,1,1,2 -- the uneven scroll that reads as lag.
// Carrying the remainder instead means the position is always an integer AND the average
// speed is exactly right; the extra pixel simply lands on the tick where the debt comes due.
void stepWhole(float& pos, float& carry, float amount, const EntityManager& em, bool horizontal,
               float otherAxis)
{
    carry += amount;
    const float whole = std::trunc(carry);
    if (whole == 0.0f)
        return;
    carry -= whole;
    const float want = pos + whole;
    const bool ok = horizontal ? walkableAt(em, want, otherAxis) : walkableAt(em, otherAxis, want);
    if (ok)
        pos = want;
    else
        carry = 0.0f; // hit a wall -- drop the debt rather than paying it into the wall
}

// Is the world walkable at this world-space point?
bool walkableAt(const EntityManager& em, float x, float y)
{
    const TileMap& map = em.tile_map;
    if (map.tile_size <= 0)
        return true;
    const int col = static_cast<int>(x) / map.tile_size;
    const int row = static_cast<int>(y) / map.tile_size;
    if (col < 0 || row < 0 || col >= map.width || row >= map.height)
        return false;
    return map.tiles[static_cast<std::size_t>(row * map.width + col)].walkable;
}
} // namespace

void bind(entt::entity player)
{
    sPlayer = player;
}

void update(Engine& /*engine*/, EntityManager& em, double dt)
{
    if (!em.registry().valid(sPlayer))
        return;

    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    pollDevKeys(keys);

    float dx = 0.0f;
    float dy = 0.0f;
    readMoveDir(keys, dx, dy);

    auto& t = em.registry().get<Transform>(sPlayer);
    if (dx != 0.0f || dy != 0.0f)
    {
        const float step = debug_panel::walkSpeed() * static_cast<float>(dt);
        // One axis at a time, so walking into a wall at an angle slides along it instead of
        // stopping dead.
        stepWhole(t.x, sCarryX, dx * step, em, /*horizontal=*/true, t.y);
        stepWhole(t.y, sCarryY, dy * step, em, /*horizontal=*/false, t.x);
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
