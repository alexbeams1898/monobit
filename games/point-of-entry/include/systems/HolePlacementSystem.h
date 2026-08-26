#pragma once

#include "ecs/EntityManager.h"
#include "ecs/GameComponents.h"
#include "formats/HoleKinds.h"
#include "formats/RoomGen.h"
#include "ops/NavUtils.h"

#include <optional>

// WHERE A HOLE SITS AND HOW IT IS DRAWN -- the geometry of one, and nothing about what it means.
//
// A hole is cut into a wall or into the ground, and everything here follows from which: which
// wall it is nearest and whether that wall leads anywhere, where its art sits and which way that
// art faces, and where its MOUTH ends up -- the spot he stands on to be offered the way through,
// which is also where its pests arrive.
//
// Lifted out of the descent because none of it is about the descent. The descent decides which
// holes a room HAS; this decides what one looks like once it has been decided on.
namespace placement
{

// How far above a marker an arch may reach for a wall, and how much rock a slab may be before it
// is architecture rather than something standing in a room.
constexpr int kArchReach = 4;
constexpr int kSlabCap = 64;

// A side's name, for anything that has to say which wall out loud.
const char* sideName(world::Side side);

// The near edge of the wall on `side` of (x, y), or negative where there is none.
float wallFace(const EntityManager& em, float x, float y, world::Side side, int depth);

// WHICH WALL A MARKER GNAWS THROUGH, or nothing where it stands in the open: the NEAREST that
// is worth gnawing. A wall is worth it where the rock behind it runs all the way out of the map
// (the hole leads somewhere new) or where it is a SLAB (an island of rock, whose far side is
// this same room -- the named exception, and what opens a pocket). Refused is the third case:
// rock and then floor again, a divider between two parts of the room he is standing in.
std::optional<world::Side> sideAt(const EntityManager& em, float x, float y, int depth);

// A hole's two faces, read from its art by TAG, and which way the art is turned to face its
// wall. Missing tags are loud: art that cannot say which frame is closed would sit there looking
// open and never react to being opened.
PlacedHole artOf(const holes::Kind* kind, world::Side side, int index);

// WHERE ITS PESTS SURFACE, and where he stands to be offered the way through. One point, so the
// thing that arrives and the thing he steps on cannot disagree.
struct Mouth
{
    float x = 0.0f;
    float y = 0.0f;
};

// Build the hole's entity at the marker and give back where its mouth ended up.
Mouth place(EntityManager& em, float mx, float my, const holes::Kind* kind, world::Side side,
            int index, int wallDepth);

} // namespace placement
