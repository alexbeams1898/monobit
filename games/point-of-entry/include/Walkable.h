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
bool stepBlocked(const EntityManager& em, float& pos, float delta, bool horizontal, float otherAxis,
                 float w, float h);

} // namespace world
