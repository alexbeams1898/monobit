#pragma once

class EntityManager;

// Is the world solid here?
//
// One implementation rather than a copy per system: the player and everything hunting him must
// agree about what a wall is, and two copies of the same tile lookup drift the moment one grows a
// special case.
namespace world
{

// World-space point. Outside the map counts as solid, so nothing can walk off the floor.
bool walkable(const EntityManager& em, float x, float y);

// Is a BOX of floor free of walls? Centred on (cx,cy); sweeps every tile the box overlaps. This
// is the movement test: a body is a box at its feet, not a point, and a point test is why half a
// character can hang into a wall before anything stops him.
bool boxFree(const EntityManager& em, float cx, float cy, float w, float h);

// Move `pos` toward `pos + delta` on one axis, stopping where the (w,h) foot box would touch a
// wall. Returns true if it moved. Per-axis so a body sliding along a wall at an angle keeps the
// component that is still free, instead of stopping dead the moment either axis is blocked.
// THE NEAREST SPOT A BOX THIS SIZE ACTUALLY FITS, searched outward from where it was asked to
// go. Callers place things by offset arithmetic -- a fixed step below a hole -- or replay a
// position out of a save written against a layout that has since been retuned; neither can know
// what the floor looks like now. Asking here is how a spot is chosen without having to.
//
// (x, y) is left where it was when it already fits. False means nothing within reach fits, which
// on a floor whose space is one connected piece means the map itself is unusable.
bool freeSpotNear(const EntityManager& em, float& x, float& y, float w, float h);

// THE BOTTOM EDGE OF A WALL standing above (x, y) within `reach` tiles, in world pixels -- or a
// negative number where there is none.
//
// A WALL HAS BODY TO IT: `depth` tiles of solid running back from its face. A partition one tile
// thick has the same room on its other side, so a thing that goes THROUGH it -- a hole gnawed
// into a cavity, a passage to somewhere else -- would be going nowhere. Rooms are carved out of
// solid rock with padding between them, so a room's own boundary always has that rock behind it,
// which is exactly what an interior divider does not.
float wallFaceAbove(const EntityManager& em, float x, float y, int reach, int depth);

// The nearest spot that both fits a box this size AND has a wall standing over it -- what a hole
// that arches into one needs. A floor's own entrance is chosen for standing room rather than for
// what is above it, so a wall-placed hole put there by inheritance has to go looking.
bool archSpotNear(const EntityManager& em, float& x, float& y, float w, float h, int reach,
                  int depth);

bool stepBlocked(const EntityManager& em, float& pos, float delta, bool horizontal, float otherAxis,
                 float w, float h);

} // namespace world

// Angles. Config authors in degrees (an arc is a number a person can picture); the math runs in
// radians, and this is the one crossing point.
namespace geom
{

constexpr float kPi = 3.14159265f;

constexpr float degToRad(float deg)
{
    return deg * kPi / 180.0f;
}

} // namespace geom
