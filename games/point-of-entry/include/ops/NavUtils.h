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

// WHICH WAY A THING FACES. A hole is cut into a wall, and which wall decides where its art
// sits, which way he steps through it, and what he sees of it -- so the side is carried rather
// than assumed. North is toward the top of the map, the direction everything faced back when
// facing was not a question anything asked.
enum class Side
{
    North,
    South,
    East,
    West,
};

// A side as a step across the grid, in tiles: one of them is zero and the other is +/-1.
// EVERYTHING directional goes through this rather than writing its own signs -- a predicate and
// the art that follows it must not be able to disagree about which way north is.
struct Step
{
    int dc = 0;
    int dr = 0;
};
Step stepOf(Side side);

// The side facing back the other way. Walking north through a hole puts him at the SOUTH wall of
// what he walks into, or the geography lies.
Side opposite(Side side);

// THE NEAR EDGE OF A WALL standing `side` of (x, y) within `reach` tiles, in world pixels along
// the axis it was found on -- or a negative number where there is none.
//
// A WALL HAS BODY TO IT: `depth` tiles of solid running back from its face. A partition one tile
// thick has the same room on its other side, so a thing that goes THROUGH it -- a hole gnawed
// into a cavity, a passage to somewhere else -- would be going nowhere. Rooms are carved out of
// solid rock with padding between them, so a room's own boundary always has that rock behind it,
// which is exactly what an interior divider does not.
float wallFace(const EntityManager& em, float x, float y, Side side, int reach, int depth);

// WHETHER THE ROCK BEHIND THE WALL `side` OF (x, y) RUNS ALL THE WAY OUT.
//
// The question a hole has to ask before it is cut: what is on the other side? Rock to the edge
// of the map means nothing of this room is back there and the hole leads somewhere new. Rock
// and then FLOOR means the wall is a divider between two parts of the room he is standing in,
// and a hole through it comes out where it went in.
//
// Thickness cannot answer this either. Rooms are stamped into solid with corridors cut between
// them, so a divider is routinely thicker than any depth worth demanding, and the rock behind it
// belongs to the same mass that reaches the map's edge -- it is only the FLOOR beyond that tells
// the two apart.
bool rockAllTheWayOut(const EntityManager& em, float x, float y, Side side);

// A SLAB STANDING INSIDE THE ROOM rather than around it: its footprint in tiles, or {0,0} where
// what stands `side` of (x, y) is the room's own edge.
//
// Thickness cannot answer this and never could. `wallFaceAbove` asks how much solid is behind a
// face and takes the answer for a boundary, but a room is built from several stamped chambers
// with corridors cut between them, so a divider standing in the middle of one is routinely
// thicker than the two tiles that test looks for. What separates the two is not how much rock
// there is but WHAT IS ON THE OTHER SIDE: keep going and a boundary runs out of the map, while a
// slab inside the room runs back into the room. A hole cut into one of those comes out where it
// went in, which is why the space beyond it cannot be more of the same floor.
//
// The footprint is the connected solid the face belongs to, so a caller can build a space that
// looks like the thing he walked into. Bounded by `cap` tiles -- past that it is architecture,
// not a slab, and gets the same answer as the edge.
struct Slab
{
    int cols = 0;
    int rows = 0;
};
Slab slabBeyond(const EntityManager& em, float x, float y, Side side, int reach, int cap);

// The nearest spot that both fits a box this size AND has a wall on the given side -- what a
// hole cut into one needs. A floor's own entrance is chosen for standing room rather than for
// what is beside it, so a wall-placed hole put there by inheritance has to go looking.
bool archSpotNear(const EntityManager& em, float& x, float& y, float w, float h, Side side,
                  int reach, int depth);

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
