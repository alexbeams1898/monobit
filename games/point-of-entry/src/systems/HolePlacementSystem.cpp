#include "systems/HolePlacementSystem.h"

#include "ecs/Components.h"
#include "ecs/FeelConfig.h"
#include "ecs/GameComponents.h"
#include "formats/SpriteDefLoader.h"
#include "ops/LogUtils.h"
#include "ops/SpawnUtils.h"

#include <cmath>
#include <string>

#include <entt/entt.hpp>

namespace placement
{
namespace
{
// The hole entity's own size, in world pixels: a tile.
constexpr float kPoeSize = 32.0f;

// A wall-placed hole arches into the wall above its marker. Both questions such a hole asks come
// from one place: whether the kind may be ASSIGNED to a marker at all, and where its art sits.
// Answering them separately is how art ends up arching into something the assignment rule never
// approved.

// Past this a slab is architecture rather than something standing in a room, and a hole in it
// is a hole in the room's edge like any other.

// How far outside the wall the mouth stands: where he waits to be offered the way through, and
// where a pest arrives.

// How far a drawing made for the wall at the TOP of a room has to come round to face another.
// The art opens downward, toward him; every other wall opens some other way.
float turnFor(world::Side side)
{
    switch (side)
    {
    case world::Side::North:
        return 0.0f;
    case world::Side::South:
        return geom::kPi;
    case world::Side::East:
        return geom::kPi * 0.5f;
    case world::Side::West:
        return -geom::kPi * 0.5f;
    }
    return 0.0f;
}

} // namespace

const char* sideName(world::Side side)
{
    switch (side)
    {
    case world::Side::North:
        return "north";
    case world::Side::South:
        return "south";
    case world::Side::East:
        return "east";
    case world::Side::West:
        return "west";
    }
    return "?";
}

float wallFace(const EntityManager& em, float x, float y, world::Side side, int depth)
{
    return world::wallFace(em, x, y, side, kArchReach, depth);
}

// A WALL WORTH GNAWING THROUGH has something on the other side. Two ways it can:
//
//   ROCK ALL THE WAY OUT -- nothing of this room is back there, so the hole leads somewhere new.
//   A SLAB -- an island of rock standing IN the room. A hole through it comes out where it went
//   in, which is why what lies beyond one is a POCKET rather than more of the descent. The
//   named exception to the rule above, and the only one.
//
// What is refused is the third case: rock, and then floor again. That is a divider between two
// parts of the room he is already standing in, and a passage through it is a passage to here.
bool wallWorthGnawing(const EntityManager& em, float x, float y, world::Side side, int depth)
{
    if (wallFace(em, x, y, side, depth) < 0.0f)
        return false;
    return world::rockAllTheWayOut(em, x, y, side) ||
           world::slabBeyond(em, x, y, side, kArchReach, kSlabCap).cols > 0;
}

// WHICH WALL A MARKER GNAWS THROUGH, or nothing where it stands in the open: the NEAREST that is
// worth gnawing. A template says where a hole may be and the room it is stamped into decides
// which of its walls that spot is against, so the answer has to be a wall the spot is actually
// beside AND one that leads anywhere.
std::optional<world::Side> sideAt(const EntityManager& em, float x, float y, int depth)
{
    std::optional<world::Side> best;
    float nearest = 0.0f;
    for (const world::Side side :
         {world::Side::North, world::Side::South, world::Side::East, world::Side::West})
    {
        if (!wallWorthGnawing(em, x, y, side, depth))
            continue;
        const float face = wallFace(em, x, y, side, depth);
        const auto [dc, dr] = world::stepOf(side);
        const float away = std::abs((dc != 0 ? x : y) - face);
        if (!best || away < nearest)
        {
            best = side;
            nearest = away;
        }
    }
    return best;
}

// A hole's two faces, read from the art by TAG. Missing tags are loud: a hole whose art cannot
// say which frame is closed would sit there looking open and never react to being opened.
PlacedHole artOf(const holes::Kind* kind, world::Side side, int index)
{
    PlacedHole art;
    art.hole = index;
    if (kind == nullptr || !kind->def.ok)
        return art;
    // A HOLE IS DRAWN AT THE ANGLE IT IS SEEN FROM, and the tag says which angle: `closed-east`
    // beside `closed`. Falling back to the undirected tag when a side has none is what lets a
    // kind be drawn one direction at a time -- an undrawn side keeps the art it has instead of
    // disappearing.
    //
    // EAST AND WEST ARE ONE DRAWING. A hole in the left wall is a hole in the right wall seen
    // from the other side, so west asks for the east tag and is mirrored when drawn; the two
    // can never drift apart because there is only one of them.
    const world::Side drawn = side == world::Side::West ? world::Side::East : side;
    bool fellBack = false;
    const auto frame = [&](const char* state)
    {
        const int sided =
            sprite_def::frameOf(kind->def, std::string{state} + "-" + sideName(drawn));
        if (sided >= 0)
            return sided;
        fellBack = true;
        return sprite_def::frameOf(kind->def, state);
    };
    const int closed = frame("closed");
    const int open = frame("opened");
    if (closed < 0 || open < 0)
    {
        poe::log().error("descent: '{}' has no closed/opened tags -- it cannot be broken open",
                         kind->def.sheet);
        return art;
    }
    // Nothing drawn for this wall: turn what there is to face it. A drawing made for one wall
    // opens toward the room, and the turn is how far that opening has to come round.
    //
    // A HOLE IN THE GROUND IS LOOKED DOWN AT and has no wall to face, so none of this applies to
    // one. Its side is whatever wall happened to be nearest its marker, which is a fact about
    // the room rather than about the hole -- turning a floor crack by it would spin the art for
    // no reason anyone could name.
    if (kind->on_wall)
    {
        art.mirrored = !fellBack && side == world::Side::West;
        if (fellBack)
            art.turn = turnFor(side);
    }
    art.closed_x = closed * kind->def.frame_w;
    art.open_x = open * kind->def.frame_w;
    return art;
}

Mouth place(EntityManager& em, float mx, float my, const holes::Kind* kind, world::Side side,
            int index, int wallDepth)
{
    const entt::entity hole = spawn::box(em, mx, my, kPoeSize, 0.75f, 0.15f, 0.15f);
    em.registry().emplace<PlacedHole>(hole, artOf(kind, side, index));
    if (kind == nullptr || !kind->def.ok)
        return Mouth{mx, my};
    auto& spr = em.registry().get<Sprite>(hole);
    spr.texture_path = kind->def.sheet;
    spr.src_x = em.registry().get<PlacedHole>(hole).closed_x; // sealed until he opens it
    spr.flip_x = em.registry().get<PlacedHole>(hole).mirrored;
    spr.rotation = em.registry().get<PlacedHole>(hole).turn;
    spr.src_w = kind->def.frame_w;
    spr.src_h = kind->def.frame_h;
    spr.layer = 1;
    em.registry().remove<SolidColor>(hole);
    if (!kind->on_wall)
        return Mouth{mx, my};
    const float face = wallFace(em, mx, my, side, wallDepth);
    if (face < 0.0f)
        return Mouth{mx, my}; // no wall to cut into; it stays where the marker put it
    // THE ART SITS IN THE WALL AND THE MOUTH ON THE FLOOR OUTSIDE IT, whichever wall it is. The
    // step says which way that is, so this cannot disagree with the predicate that found the
    // wall in the first place.
    const auto ts = static_cast<float>(em.tile_map.tile_size);
    const auto [dc, dr] = world::stepOf(side);
    const float half = static_cast<float>(dc != 0 ? kind->def.frame_w : kind->def.frame_h) * 0.5f;
    auto& t = em.registry().get<Transform>(hole);
    // Centred in its own tile on the axis it runs along, so a hole never straddles two cells.
    t.x = dc != 0 ? face + static_cast<float>(dc) * half : std::floor(mx / ts) * ts + ts * 0.5f;
    t.y = dr != 0 ? face + static_cast<float>(dr) * half : std::floor(my / ts) * ts + ts * 0.5f;
    em.registry().get<Sprite>(hole).layer = 2;
    return Mouth{dc != 0 ? face - static_cast<float>(dc) * feel::current().reek.stand : t.x,
                 dr != 0 ? face - static_cast<float>(dr) * feel::current().reek.stand : t.y};
}

} // namespace placement
