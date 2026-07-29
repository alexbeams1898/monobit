#include "PlayerMovement.h"

#include "ecs/Components.h"
#include "ecs/EntityManager.h"

#include <SDL.h>

#include <cmath>

namespace player_movement
{
namespace
{
// Shrink the test box slightly so the character slides past tile corners
// instead of catching on a 1-2px overlap. See prison-escape MovementSystem's
// MOVEMENT_INSET note for the corner-sticking rationale.
constexpr float kInset = 2.0f;

// True if two center-based AABBs overlap.
bool aabbOverlap(float ax, float ay, float aw, float ah, float bx, float by, float bw, float bh)
{
    return std::abs(ax - bx) * 2.0f < (aw + bw) && std::abs(ay - by) * 2.0f < (ah + bh);
}

// True if a center-based box (cx,cy,w,h) would stand in any non-walkable tile OR any
// OTHER entity's solid Collider -- props and creatures alike, so a wandering cat
// refuses to step onto the player exactly as the player refuses its box. Terrain
// lives in the tile map; colliders carry pixel-tight boxes the grid can't represent,
// so both are tested together. One rider: a solid already overlapping self's CURRENT
// box is exempt -- blocked means "would ENTER a solid", never "is inside one" --
// so however an overlap came to be (a scene walking someone through you, a same-tick
// crossing), moving out is always possible and collision can never imprison.
bool touchesSolid(const EntityManager& em, entt::entity self, float cx, float cy, float w, float h)
{
    const auto& tm = em.tile_map;
    if (tm.valid())
    {
        const float ts = static_cast<float>(tm.tile_size);
        const float hw = w * 0.5f;
        const float hh = h * 0.5f;
        const int cmin = static_cast<int>(std::floor((cx - hw) / ts));
        const int cmax = static_cast<int>(std::floor((cx + hw) / ts));
        const int rmin = static_cast<int>(std::floor((cy - hh) / ts));
        const int rmax = static_cast<int>(std::floor((cy + hh) / ts));
        for (int r = rmin; r <= rmax; ++r)
            for (int c = cmin; c <= cmax; ++c)
                if (!tm.in_bounds(c, r) || !tm.at(c, r).walkable)
                    return true;
    }
    const auto& reg = em.registry();
    const auto* selfT = reg.try_get<Transform>(self);
    auto view = reg.view<const Transform, const Collider>();
    for (auto [e, t, col] : view.each())
    {
        if (e == self || !col.is_solid)
            continue; // you are not in your own way
        if (!aabbOverlap(cx, cy, w, h, t.x, t.y, col.width, col.height))
            continue;
        if (selfT != nullptr &&
            aabbOverlap(selfT->x, selfT->y, w, h, t.x, t.y, col.width, col.height))
            continue; // already inside it -- escaping, not entering
        return true;
    }
    return false;
}

// Which perpendicular DIRECTION to deflect a blocked move: +1, -1, or 0 (boxed in). The
// caller then spends the frame's movement budget that way, so deflection REDIRECTS your speed
// along the surface rather than adding to it -- the difference between a smooth glide and a
// jerky sideways lurch.
//
// Looks a probe distance to each side (the nearer side first, so you round the short way) and
// picks the first that's clear at the destination AND along the path there (so a diagonal gap
// can't be slipped through). `probe` is how far to look for clear space -- a few px, enough to
// tell "there's an opening this way" without teleporting. `blockedX` picks the axis: blocked
// in X deflects along Y, and vice versa.
int deflectDir(const EntityManager& em, entt::entity self, float bx, float by, float w, float h,
               bool blockedX, float probe)
{
    if (probe <= 0.0f)
        return 0;
    for (const int dir : {1, -1})
    {
        const float s = static_cast<float>(dir) * probe;
        const float px = blockedX ? bx : bx + s;
        const float py = blockedX ? by + s : by;
        const float pathX = blockedX ? bx : px;
        const float pathY = blockedX ? py : by;
        if (!touchesSolid(em, self, px, py, w, h) && !touchesSolid(em, self, pathX, pathY, w, h))
            return dir;
    }
    return 0; // no clear side -- boxed in, so stop
}

// WASD/arrow keys -> a normalized move direction (diagonals aren't faster than
// cardinals). Zero vector when no movement key is held.
void readMoveDir(const unsigned char* keys, float& ix, float& iy)
{
    ix = 0.0f;
    iy = 0.0f;
    if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP])
        iy -= 1.0f;
    if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN])
        iy += 1.0f;
    if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT])
        ix -= 1.0f;
    if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT])
        ix += 1.0f;
    const float len = std::sqrt(ix * ix + iy * iy);
    if (len > 0.0f)
    {
        ix /= len;
        iy /= len;
    }
}

} // namespace

MoveIntent update(EntityManager& em, entt::entity player, const unsigned char* keys, float speed,
                  float corner_nudge, float corner_slide, float fdt)
{
    auto& reg = em.registry();
    if (!reg.valid(player))
        return {};

    auto* t = reg.try_get<Transform>(player);
    auto* v = reg.try_get<Velocity>(player);
    const auto* col = reg.try_get<Collider>(player);
    if (!t || !v || !col)
        return {};

    float ix = 0.0f;
    float iy = 0.0f;
    readMoveDir(keys, ix, iy);
    v->dx = ix * speed;
    v->dy = iy * speed;

    const float mw = col->width - kInset;
    const float mh = col->height - kInset;

    // This frame's deflection budget (px): the sideways glide spends THIS, redirected along
    // the wall, rather than adding to the blocked move -- so it's smooth, not a lurch. Scaled
    // by corner_slide so the glide can run gentler than a free walk (the "slidy" pace knob).
    const float budget = speed * corner_slide * fdt;
    // How far to look sideways for an opening. `corner_nudge` widens the search (rounds a
    // corner from further out) without affecting how FAST you deflect -- that's the budget.
    const float probe = std::max(budget, corner_nudge);

    // Axis-split projection: resolve X, then Y from the new X. Each axis: try the move; if
    // blocked, drop it and glide perpendicular toward the open side by the budget (go AROUND,
    // not through). The two branches are deliberate mirror images -- parameterizing them by
    // axis traded readability for sign-bug risk, so they stay spelled out.
    float nx = t->x + v->dx * fdt;
    if (v->dx != 0.0f && touchesSolid(em, player, nx, t->y, mw, mh))
    {
        nx = t->x; // X blocked -> drop the forward move
        const int dir = deflectDir(em, player, t->x + v->dx * fdt, t->y, mw, mh, true, probe);
        const float dy = static_cast<float>(dir) * budget;
        if (dir != 0 && !touchesSolid(em, player, nx, t->y + dy, mw, mh))
            t->y += dy;
    }
    float ny = t->y + v->dy * fdt;
    if (v->dy != 0.0f && touchesSolid(em, player, nx, ny, mw, mh))
    {
        ny = t->y; // Y blocked -> drop the forward move
        const int dir = deflectDir(em, player, nx, t->y + v->dy * fdt, mw, mh, false, probe);
        const float dx = static_cast<float>(dir) * budget;
        if (dir != 0 && !touchesSolid(em, player, nx + dx, ny, mw, mh))
            nx += dx;
    }
    t->x = nx;
    t->y = ny;
    return {ix, iy};
}

bool canStand(const EntityManager& em, entt::entity entity, float wx, float wy)
{
    const auto* col = em.registry().try_get<Collider>(entity);
    if (col == nullptr)
        return false;
    // The same inset box update() projects with, so "somewhere I could walk to" and
    // "somewhere I can be placed" can never disagree.
    return !touchesSolid(em, entity, wx, wy, col->width - kInset, col->height - kInset);
}
} // namespace player_movement
