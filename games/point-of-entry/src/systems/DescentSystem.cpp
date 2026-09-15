#include "systems/DescentSystem.h"

#include "Engine.h"
#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "ecs/FeelConfig.h"
#include "ecs/GameComponents.h"
#include "formats/FloorTypes.h"
#include "formats/HoleKinds.h"
#include "formats/RoomGen.h"
#include "formats/SpriteDefLoader.h"
#include "ops/LogUtils.h"
#include "ops/NavUtils.h"
#include "ops/SpawnUtils.h"
#include "systems/HolePlacementSystem.h"
#include "systems/PlayerSystem.h"
#include "systems/TileMapRenderer.h"
#include "systems/TravelSystem.h"
#include "systems/WaveSystem.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <optional>
#include <random>
#include <unordered_map>
#include <vector>

#include <entt/entt.hpp>

namespace descent
{
namespace
{

constexpr float kPoeSize = 32.0f;

std::vector<Room> sRooms;

// WHERE THE HOLES OF THE ROOM HE IS IN PHYSICALLY STAND, rebuilt on arrival and identical every
// time because the layout is. Only ever the room being built or stood in: a room he is not in
// has no positions, because it has no world. Kept out of Room for exactly that reason -- Room is
// the save's shape, and a thing that is rebuilt has no business persisting.
std::vector<swarm::Hole> sMouths;

// A HOLE CARRIES THE DEPTH ITS PROGRAM RUNS AT, and it is the depth of the ROOM that owns it --
// which is how the Law of Depth reaches an actual fight rather than stopping at the floor's base
// curve. A hole CARRIED from somewhere else keeps the depth it came from, which is the whole
// reason the depth rides on the hole instead of being read off the room here.
//
// ONE RECORD PER HOLE THE SWARM IS RUNNING, in the swarm's own order -- which is hole order,
// one slot per hole of the floor he stands on. `owner` is whose program it is: this floor's own
// hole, or a hole on the far side of the passage that this hole has become. Asking the slot is
// the ONLY way to tell the two apart. Working it out three ways is how this went wrong before:
// two parallel arrays tied together by an index offset, and a heuristic comparing room ids.
struct HoleSlot
{
    Link owner;  // whose program is running here
    int at = -1; // which of THIS floor's holes it arrives at
    float x = 0.0f;
    float y = 0.0f;
};
std::vector<HoleSlot> sSlots;
int sCurrent = -1; // room the player stands in; -1 = not in the dig

// IS ANYTHING ACTUALLY RUNNING on this floor -- a hole he broke open and did not
// finish? Not "does it have holes left": a floor nobody has disturbed is quiet by
// definition, because a sealed hole sends nothing. One definition, asked of the
// floor he stands on (is this work?) and of the floor below a hole (is that hole
// in_use?), so the two can never disagree about what an unfinished floor is.
bool workUnderway(int roomId)
{
    if (roomId < 0 || roomId >= static_cast<int>(sRooms.size()))
        return false;
    for (const Hole& hole : sRooms[static_cast<std::size_t>(roomId)].holes)
        if (hole.opened && !hole.cleared)
            return true;
    return false;
}

// A WAY DOWN LEAKS BECAUSE HE LEFT SOMETHING RUNNING. Not because a floor exists
// below it: an undug floor has nothing coming out of it, and a dug one he never
// broke anything open on has nothing either. What comes up the hole is what he
// disturbed and walked away from -- so a leak is a report on his own unfinished
// business, and a quiet hole means there is nothing down there to answer for.
// The floor a hole leads to, or -1 where it has never been dug.
int beyond(int room, int hole)
{
    if (room < 0 || room >= static_cast<int>(sRooms.size()) || hole < 0)
        return -1;
    const auto& holes = sRooms[static_cast<std::size_t>(room)].holes;
    return hole < static_cast<int>(holes.size()) ? holes[static_cast<std::size_t>(hole)].to.room
                                                 : -1;
}

// What a KIND OF SPACE draws its holes from: the mix, the letter-pinned kinds, and its rules.
struct KindTable
{
    std::vector<holes::Kind> kinds;
    std::unordered_map<char, std::string> pinned;
    holes::Rules rules;
    int total_weight = 0;
};

void beginFloor(int roomId);

// IT REEKS WHEN IT IS WORKING. A hole he has opened and not finished, or a passage carrying
// something up from below, gives off a slow rise of vapour -- and one that has nothing to send
// gives off none, which is the same thing the Descend prompt is saying, said in the world
// instead of in words. Puffs alternate their drift so the column wanders as it climbs.

constexpr int kReekBlobs = 5; // a cloud is a LUMP: several offset blobs read as one soft mass

float sReekTimer = 0.0f;
int sReekSide = 0;

void restoreSpentHoles(EntityManager& em, int roomId)
{
    Room& room = sRooms[static_cast<std::size_t>(roomId)];
    auto& reg = em.registry();
    // The hole's ONE component is already on the entity -- placement put it there to carry the
    // art. What arriving adds is the rest of it: where its mouth ended up this visit, and the
    // reach that makes it offerable once it is spent. Written INTO the component rather than
    // over it, or the faces it was drawn with go with the assignment.
    for (auto [e, placed] : reg.view<PlacedHole>().each())
    {
        const auto i = static_cast<std::size_t>(placed.hole);
        if (placed.hole < 0 || i >= room.holes.size() || i >= sMouths.size())
            continue;
        const Hole& hole = room.holes[i];
        if (hole.opened)
            if (auto* spr = reg.try_get<Sprite>(e))
                spr->src_x = placed.open_x;
        placed.mouth_x = sMouths[i].x;
        placed.mouth_y = sMouths[i].y;
        // A SPENT hole is a passage and can be stood on; anything else is scenery, and a reach
        // of nothing is what keeps the prompt off it.
        placed.reach = hole.cleared ? holeReach() : 0.0f;
    }
}

// EVERY act_every-th depth is one floor that every branch leads into. 0 disables it, and the
// descent goes back to widening forever.
int actEvery()
{
    static const int every = []
    {
        std::ifstream in("config/descent.json");
        const nlohmann::json j =
            in ? nlohmann::json::parse(in, nullptr, /*allow_exceptions=*/false) : nlohmann::json{};
        if (j.is_discarded() || !j.is_object())
            return 4;
        return j.value("descent", nlohmann::json::object()).value("act_every", 4);
    }();
    return every;
}

// The floor every branch at this depth shares, if it has been dug. Convergence is a LINK, not
// a copy: the second hole to reach it opens the same room the first one did.
int sharedAt(int depth)
{
    for (std::size_t i = 0; i < sRooms.size(); ++i)
        if (sRooms[i].depth == depth && sRooms[i].area.empty())
            return static_cast<int>(i);
    return -1;
}

// A..Z, then AA, AB -- the spreadsheet column scheme, so a depth can hold any number of rooms
// without the letters ever borrowing a digit.
std::string roomLetter(int index)
{
    std::string out;
    for (int n = index + 1; n > 0; n = (n - 1) / 26)
        out.insert(out.begin(), static_cast<char>('A' + (n - 1) % 26));
    return out;
}

// The tag a NEW room at this depth would take: its place in the order rooms at that depth were
// dug, which is why it never moves once written.
std::string labelFor(int depth)
{
    int rooms = 0;
    for (const auto& room : sRooms)
        if (room.depth == depth)
            ++rooms;
    return "B" + std::to_string(depth) + "-" + roomLetter(rooms);
}

int indexOfArea(const std::string& area)
{
    for (std::size_t i = 0; i < sRooms.size(); ++i)
        if (sRooms[i].area == area)
            return static_cast<int>(i);
    return -1;
}

// AN AUTHORED LEVEL WITH HOLES IN IT IS A FLOOR. Found by name, so its spent
// holes and the floors under them persist exactly as a dug floor's do; adopted
// from the world itself rather than from an event, so it makes no difference
// whether he walked in, climbed up, or woke here after a bad day.
//
// Hole numbers come from a sorted order, so a hole keeps its identity -- and
// everything spent behind it -- across every visit.
int adoptArea(EntityManager& em, const std::string& area)
{
    auto& reg = em.registry();
    std::vector<entt::entity> holes;
    for (const auto e : reg.view<AuthoredHole, Transform>())
        holes.push_back(e);
    if (holes.empty())
        return -1; // a room with nothing coming into it is not a floor
    std::sort(holes.begin(), holes.end(),
              [&](entt::entity a, entt::entity b)
              {
                  const auto& ta = reg.get<Transform>(a);
                  const auto& tb = reg.get<Transform>(b);
                  return ta.y != tb.y ? ta.y < tb.y : ta.x < tb.x;
              });

    int id = indexOfArea(area);
    if (id < 0)
    {
        Room room;
        room.area = area;
        room.label = labelFor(room.depth);
        sRooms.push_back(std::move(room));
        id = static_cast<int>(sRooms.size()) - 1;
    }
    Room& room = sRooms[static_cast<std::size_t>(id)];
    const std::vector<Hole> kept = room.holes;
    sMouths.clear();
    room.holes.clear();
    for (std::size_t i = 0; i < holes.size(); ++i)
    {
        const auto& at = reg.get<Transform>(holes[i]);
        const holes::Kind kind = holes::load(reg.get<AuthoredHole>(holes[i]).kind);
        // A hole wears its KIND's face wherever it is: the map says where one is and what sort,
        // never what it looks like.
        const world::Side side = placement::sideAt(em, at.x, at.y, holes::Rules{}.wall_depth)
                                     .value_or(world::Side::North);
        const PlacedHole art = placement::artOf(&kind, side, static_cast<int>(i));
        if (kind.def.ok)
        {
            auto& spr = reg.get<Sprite>(holes[i]);
            spr.texture_path = kind.def.sheet;
            spr.src_x = art.closed_x;
            spr.src_w = kind.def.frame_w;
            spr.src_h = kind.def.frame_h;
            spr.layer = 1;
            spr.flip_x = art.mirrored;
            spr.rotation = art.turn;
            reg.remove<SolidColor>(holes[i]);
        }
        sMouths.push_back(swarm::Hole{at.x, at.y, kind.path});
        room.holes.push_back(Hole{Link{}, kind.path, side, false, false, 0});
        reg.emplace_or_replace<PlacedHole>(holes[i], art);
    }
    // An authored floor keeps whatever state the save brought back for holes the map still has.
    for (std::size_t i = 0; i < room.holes.size() && i < kept.size(); ++i)
    {
        room.holes[i].to = kept[i].to;
        room.holes[i].opened = kept[i].opened;
        room.holes[i].cleared = kept[i].cleared;
        room.holes[i].killed = kept[i].killed;
    }

    restoreSpentHoles(em, id);
    beginFloor(id);
    poe::log().info("descent: '{}' is a floor -- {} hole(s) at depth {}", area, sMouths.size(),
                    room.depth);
    return id;
}

// The floor he is standing in follows the world. Generated space is whatever travelling a
// passage put him in; an authored level is a floor when it has holes and is not one when it
// does not. An authored hole wearing no number yet is how a freshly built level announces
// itself, so this needs no event to listen for.
// WHICH ROOM HE IS STANDING IN, worked out when the ground under him CHANGES rather than every
// frame. What it costs is a walk of the whole descent comparing area names, which grows with
// every room ever dug and answers the same thing all the way down until he goes somewhere else.
//
// The trigger is the area's name and whether the room has been built yet -- the two inputs the
// answer is made of. Anything else that could change it changes one of those first.
// What the answer above was last settled for. File-scope rather than function-static because
// REPLACING THE TREE unsettles it -- a reset or a restore leaves the same man standing in the
// same room while every room id under him changes, and an answer cached across that names a
// room that no longer means what it did.
std::string sSettledArea;
bool sSettledBuilt = false;

void forgetSettledArea()
{
    sSettledArea.clear();
    sSettledBuilt = false;
}

void syncArea(EntityManager& em)
{
    const std::string& area = travel::currentArea();
    if (area.empty())
        return;
    auto& reg = em.registry();
    const bool built = !reg.view<AuthoredHole>().empty();
    if (area == sSettledArea && built == sSettledBuilt)
        return;
    sSettledArea = area;
    sSettledBuilt = built;
    if (!built)
    {
        sCurrent = -1; // a room he is only passing through
        return;
    }
    bool fresh = false;
    for (const auto e : reg.view<AuthoredHole>())
        if (!reg.all_of<PlacedHole>(e))
        {
            fresh = true;
            break;
        }
    sCurrent = fresh ? adoptArea(em, area) : indexOfArea(area);
}

// WHAT A DEPTH IS ALREADY COMMITTED TO: the rooms it holds, plus the rooms it has PROMISED.
//
// A hole in a wall that leads nowhere yet is a room that does not exist and will: travelling it
// digs one, and nothing between here and there can decide otherwise. Counting only the rooms
// that exist is counting deliveries and ignoring orders -- and since a room carries two or three
// wall holes, a depth allowed five rooms settles at ten or more. A promise counts as the room it
// is going to be.
//
// A depth is one floor of the world however many ways down reach it, so this counts all of them:
// counting only what he can walk to sideways would give every way down its own allowance.
int roomsPromisedAt(int depth)
{
    int rooms = 0;
    for (std::size_t n = 0; n < sRooms.size(); ++n)
    {
        const Room& room = sRooms[n];
        if (room.depth != depth)
            continue;
        ++rooms;
        for (std::size_t i = 0; i < room.holes.size(); ++i)
            if (room.holes[i].to.room < 0 && !descends(static_cast<int>(n), static_cast<int>(i)))
                ++rooms;
    }
    return rooms;
}

// IS THIS DEPTH FULL? A run sideways expands OUTWARD from wherever he first came down and stops
// when the floor is committed to as many rooms as its kind of space is allowed. `granted` is what
// the floor being built has been handed already, since its own holes are promises the moment they
// are drawn and the count has to see them coming.
//
// The limit belongs to the space a wall hole would OPEN rather than to the one it is cut into: a
// gnawed run makes a warren, and how wide a warren spreads is a fact about warrens.
bool depthIsFull(int depth, const holes::Kind& kind, int granted)
{
    std::vector<holes::Kind> kinds;
    std::unordered_map<char, std::string> pinned;
    const holes::Rules rules =
        holes::loadTable(kind.opens.empty() ? holes::defaultType() : kind.opens, kinds, pinned);
    return roomsPromisedAt(depth) + granted >= rules.rooms_per_floor;
}

// WHICH KIND OF HOLE A MARKER BECOMES: a pinned letter takes its named kind, anything else
// rolls the floor's weighted mix. A wall kind rolled onto a marker with no wall in reach falls
// back to the table's first floor kind -- deterministically, with no extra roll, so the seed
// still reproduces the floor exactly.
// EVERYTHING A MARKER NEEDS to become a hole: the kinds this room may grow, the letters pinned
// to one, and the rules the space imposes. Passed together because they are read together and
// come from one file -- nine loose arguments is a signature nobody calls correctly twice.
// WHETHER A HOLE IN A WALL MAY STAND AT THIS MARKER. Three ways it may not: this room is a
// POCKET, which grows none (a slab has the same room on both sides, so a passage through one of
// its walls comes out where it went in); no wall beside the marker is worth gnawing; or the
// depth it would open onto has no room left.
//
// A refusal is not a re-roll. The caller falls back to the table's first floor kind, with no
// extra draw, so the seed still reproduces the floor exactly and nothing anywhere has to refuse
// a hole that is already drawn.
bool wallKindStands(const EntityManager& em, const roomgen::Marker& m, const KindTable& table,
                    const holes::Kind& kind, int depth, int granted, bool pocket)
{
    if (pocket)
        return false;
    if (!placement::sideAt(em, m.x, m.y, table.rules.wall_depth))
        return false;
    return !depthIsFull(depth, kind, granted);
}

const holes::Kind* chooseKind(const EntityManager& em, const roomgen::Marker& m, KindTable& table,
                              std::mt19937& rng, int depth, int& granted, bool pocket)
{
    const auto pin = table.pinned.find(m.type);
    // A PIN IS AN INTENT, NOT AN EXEMPTION. An authored letter saying "this spot is a gnawed
    // gap" still needs a wall to gnaw through, so it falls through to the same check a rolled
    // kind faces -- a mouse hole in open floor is not a mouse hole.
    const holes::Kind* kind = pin != table.pinned.end()
                                  ? holes::byPath(table.kinds, pin->second)
                                  : (table.kinds.empty() ? nullptr : &table.kinds.front());
    if (pin == table.pinned.end() && table.total_weight > 0)
    {
        std::uniform_int_distribution<int> roll(0, table.total_weight - 1);
        int ticket = roll(rng);
        for (const auto& k : table.kinds)
        {
            ticket -= k.weight;
            if (ticket < 0)
            {
                kind = &k;
                break;
            }
        }
    }
    if (kind != nullptr && kind->on_wall &&
        !wallKindStands(em, m, table, *kind, depth, granted, pocket))
        for (const auto& k : table.kinds)
            if (!k.on_wall)
                return &k;
    if (kind != nullptr && kind->on_wall)
        ++granted; // one more room this floor has just promised the depth
    return kind;
}

// THE WAY IN, placed beside the floor's own entrance and drawn as the hole it is the far end
// of. Anything placed by offset must land on floor: a fixed nudge from the entrance can sit
// inside a wall in a tight room.
void placeWayIn(EntityManager& em, Room& room, const roomgen::Layout& floor,
                std::vector<holes::Kind>& kinds, int wallDepth)
{
    const holes::Kind* kind = holes::byPath(kinds, room.holes.front().kind);
    const world::Side wayInSide = room.holes.front().side;
    // THE WAY IN IS THE FAR END OF THE HOLE HE CAME THROUGH, so it wears that hole's kind -- and
    // a kind that arches into a wall needs a wall wherever that puts it. The floor's own entrance
    // is chosen for standing room and knows nothing about what is above it, so the spot is
    // searched for rather than nudged to: near the entrance if it can be, at the nearest wall
    // otherwise.
    float ux = floor.spawn_x;
    float uy = floor.spawn_y;
    constexpr float kPoint = 1.0f; // a spot, not a body: standAt fits the man to it on arrival
    const bool sited = kind != nullptr && kind->on_wall
                           ? world::archSpotNear(em, ux, uy, kPoint, kPoint, wayInSide,
                                                 placement::kArchReach, wallDepth)
                           : world::freeSpotNear(em, ux, uy, kPoint, kPoint);
    if (!sited)
    {
        // A floor with no wall to arch into is a floor that failed to build, but he still has to
        // come out somewhere -- standable beats correct-looking.
        poe::log().error("descent: no wall for the way in on {} -- placing it on open floor",
                         room.label);
        world::freeSpotNear(em, ux, uy, kPoint, kPoint);
    }
    if (kind != nullptr && kind->on_wall)
        poe::log().debug("descent: way in wears '{}' in the {} wall", room.holes.front().kind,
                         placement::sideName(wayInSide));
    else
        poe::log().debug("descent: way in wears '{}' in the ground", room.holes.front().kind);
    const placement::Mouth mouth = placement::place(em, ux, uy, kind, wayInSide, 0, wallDepth);
    sMouths[0] = swarm::Hole{mouth.x, mouth.y, room.holes.front().kind};
}

// WHICH KIND A HOLE KEEPS between visits. A FLOOR IS THE SAME PLACE EVERY VISIT: a hole keeps
// the kind it was first given, so retuning the table cannot change what is in a room he has
// already stood in. `rolled` is what this marker would take if it were fresh, which is both the
// answer for a new hole and the replacement for a broken one.
const std::string& keptKind(Hole& hole, const std::string& rolled, const EntityManager& em,
                            const roomgen::Marker& m, int wallDepth, const std::string& tag)
{
    // What it may NOT keep is a kind that cannot exist where it stands. A hole in a wall with no
    // wall to it was never a place, it was a fault -- and preserving a fault faithfully means
    // every floor dug before the rule existed keeps a hole drawn in open floor forever. Only
    // that direction is repaired: a floor kind is left alone even where a wall has since become
    // available, because THAT would be the reshuffling the rule above exists to prevent.
    if (!hole.kind.empty() && holes::onWall(hole.kind) &&
        placement::wallFace(em, m.x, m.y, hole.side, wallDepth) < 0.0f)
    {
        poe::log().info("descent: {} kept '{}' with no wall to it -- it is '{}' now", tag,
                        hole.kind, rolled);
        hole.kind.clear();
    }
    if (hole.kind.empty())
        hole.kind = rolled;
    // Resolved once, AFTER every decision: kindByPath appends to `kinds` for a path it has
    // not seen, and a pointer taken before that append is a pointer into the old buffer.
    return hole.kind;
}

// ONE MARKER BECOMES ONE HOLE: what kind it is, which wall it is cut into, the wall opened where
// he could not otherwise see in, and the art placed. Returns where its mouth ended up.
// WHICH HOLE OF WHICH ROOM is being settled -- the four things that only ever travel together.
struct Slot
{
    const Room& room;
    Hole& hole;
    int index = 0;
    int room_id = 0;
};

placement::Mouth settleHole(EntityManager& em, const roomgen::Marker& m, KindTable& table,
                            std::mt19937& rng, const Slot& at, int& granted)
{
    const Room& room = at.room;
    Hole& hole = at.hole;
    const holes::Kind* kind =
        chooseKind(em, m, table, rng, room.depth, granted, room.slab_cols > 0);
    const std::string rolled = kind != nullptr ? kind->path : std::string{};
    // DERIVED EVERY BUILD, never kept. Which wall a marker is nearest is a fact about the map,
    // and the map comes back identical from its seed -- so a stored copy is a second source for
    // one truth, and the day they disagree the map is right and the record is stale. It would
    // also mean a room dug under an older rule answered by that rule forever, leaving the
    // descent a patchwork of whatever was true the day each room was first entered.
    //
    // The KIND is kept, and the difference is the point: a kind is a ROLL off a weighted table,
    // so keeping it is what stops a retune rewriting a room he has already stood in. A wall is
    // not a roll.
    // Only a hole in a WALL has one. A hole in the ground faces up at him, and giving it the
    // nearest wall's direction would be recording an accident of the room as a fact about the
    // hole -- something later code would eventually read and believe.
    hole.side =
        kind != nullptr && kind->on_wall
            ? placement::sideAt(em, m.x, m.y, table.rules.wall_depth).value_or(world::Side::North)
            : world::Side::North;
    // Resolved once, AFTER every decision: kindByPath appends to `table.kinds` for a path it has
    // not seen, and a pointer taken before that append is a pointer into the old buffer.
    kind = holes::byPath(table.kinds, keptKind(hole, rolled, em, m, table.rules.wall_depth,
                                               poeTag(at.room_id, at.index)));
    if (kind != nullptr && kind->on_wall)
        poe::log().debug("descent: hole {} wears '{}' in the {} wall", at.index, hole.kind,
                         placement::sideName(hole.side));
    else
        poe::log().debug("descent: hole {} wears '{}' in the ground", at.index, hole.kind);
    return placement::place(em, m.x, m.y, kind, hole.side, at.index, table.rules.wall_depth);
}

bool buildNode(EntityManager& em, int roomId, float& spawnX, float& spawnY)
{
    Room& room = sRooms[static_cast<std::size_t>(roomId)];
    auto& reg = em.registry();
    // The world he came from goes; he does not.
    {
        std::vector<entt::entity> gone;
        for (const auto e : reg.view<Transform>())
            if (e != player::entity())
                gone.push_back(e);
        for (const auto e : gone)
            reg.destroy(e);
    }
    travel::leaveAuthored();
    em.tile_config = TileConfig{}; // the floor's own tile vocabulary

    const roomgen::Layout floor = roomgen::generate(
        em, holes::typeOf(room), room.seed, roomgen::Extent{room.slab_cols, room.slab_rows});
    if (!floor.ok)
    {
        poe::log().error("descent: could not build floor at depth {}", room.depth);
        return false;
    }
    room.seed = floor.seed; // first generation picks; every later visit repeats
    spawnX = floor.spawn_x;
    spawnY = floor.spawn_y;

    // THE WAY HE CAME IN IS HOLE ZERO, placed before the floor's own. It is a hole like the
    // rest -- same art, same mouth, same passage rules -- that simply arrives already spent,
    // which is what a passage is. A floor with nothing above it has none and starts at its own.
    const int offset = room.way_in >= 0 ? 1 : 0;
    sMouths.assign(static_cast<std::size_t>(offset), swarm::Hole{});

    KindTable table;
    table.rules = holes::loadTable(holes::typeOf(room), table.kinds, table.pinned);
    for (const auto& k : table.kinds)
        table.total_weight += k.weight;

    std::mt19937 rng(floor.seed * 2654435761u + 97u);
    int granted = 0; // wall holes handed out on this floor, which the depth is committed to
    int index = offset;
    for (const auto& m : floor.markers)
    {
        if (m.type != 'P' && table.pinned.find(m.type) == table.pinned.end())
            continue;
        const auto slot = static_cast<std::size_t>(index);
        if (slot >= room.holes.size())
            room.holes.resize(slot + 1);
        const placement::Mouth mouth =
            settleHole(em, m, table, rng, Slot{room, room.holes[slot], index, roomId}, granted);
        sMouths.push_back(swarm::Hole{mouth.x, mouth.y, room.holes[slot].kind});
        ++index;
    }
    room.holes.resize(static_cast<std::size_t>(index));

    if (offset > 0)
        placeWayIn(em, room, floor, table.kinds, table.rules.wall_depth);

    restoreSpentHoles(em, roomId);
    beginFloor(roomId);
    return true;
}

// Enter a room: build, upload, and stand the player at (x,y) -- or at the
// floor's own way in when the caller passes atWayIn.
// Arriving on an authored floor is TRAVEL, not generation -- the map already
// holds the space, and the level's own start is where he lands unless the
// caller names the spot he is climbing out at.
// Standing at one of a floor's holes -- climbing out of it, in front of its
// mouth. Asked only AFTER the floor is built: a hole's position is rebuilt on
// arrival, so a floor nobody has walked into this sitting has none yet, and a
// caller that worked one out in advance would be reading a floor that does not
// exist. Returns false when the hole is not one this floor has.
bool standAtHole(EntityManager& em, int hole)
{
    if (hole < 0 || hole >= static_cast<int>(sMouths.size()))
        return false;
    const auto& at = sMouths[static_cast<std::size_t>(hole)];
    player::standAt(em, at.x, at.y + 24.0f);
    return true;
}

bool enterAuthored(Engine& engine, EntityManager& em, int roomId, int atHole)
{
    const std::string area = sRooms[static_cast<std::size_t>(roomId)].area;
    if (!travel::enter(engine, em, area))
    {
        poe::log().error("descent: could not climb out into '{}'", area);
        return false;
    }
    sCurrent = adoptArea(em, area);
    if (sCurrent < 0)
        return false;
    standAtHole(em, atHole); // otherwise the level's own start stands
    return true;
}

// `atHole` names the hole he arrives at; -1 falls back to the floor's own way in, which is
// what arriving without having come through anything means.
bool enterNode(Engine& engine, EntityManager& em, int roomId, int atHole)
{
    if (!sRooms[static_cast<std::size_t>(roomId)].area.empty())
        return enterAuthored(engine, em, roomId, atHole);
    float sx = 0.0f;
    float sy = 0.0f;
    if (!buildNode(em, roomId, sx, sy))
        return false;
    TileMapRenderer::upload(em.tile_map, em.tile_config, engine.textureManager());
    em.flow_field.last_player_col = -1;
    em.flow_field.last_player_row = -1;
    // In front of the hole he came out of, never on it, so no prompt greets the landing. A
    // floor with no way in and no named hole falls back to the space's own start.
    if (!standAtHole(em, atHole) &&
        !standAtHole(em, sRooms[static_cast<std::size_t>(roomId)].way_in))
        player::standAt(em, sx, sy);
    sCurrent = roomId;
    const Room& here = sRooms[static_cast<std::size_t>(roomId)];
    poe::log().info("descent: at {} (depth {}, room {}, seed {})", here.label, here.depth, roomId,
                    here.seed);
    return true;
}

// EVERYTHING UNFINISHED ON THE FAR SIDE OF ONE PASSAGE, nearest floor first. What he broke
// open and walked away from is still coming, however far he has travelled since and however
// long the trail of abandoned holes runs -- but a passage is one passage, so they arrive
// through it in turn rather than all at once.
//
// `from` is where he stands and the walk never re-enters it, which is the whole of what used to
// be two functions: what lies BELOW a way down and what lies BEHIND the way he came in are the
// same question asked through different holes. Direction never enters into it, which is what
// lets a hole in a wall answer it identically to a hole in the ground.
//
// Breadth-first, so the floor immediately across reaches him before what is beyond that; and
// visited, because the graph is not quite a tree: branches rejoin at an act boundary, so a
// rejoined floor would otherwise be counted once per branch that reaches it.
std::vector<Link> queueThrough(int from, int via)
{
    std::vector<Link> found;
    if (via < 0 || via >= static_cast<int>(sRooms.size()))
        return found;
    std::vector<bool> seen(sRooms.size(), false);
    if (from >= 0 && from < static_cast<int>(sRooms.size()))
        seen[static_cast<std::size_t>(from)] = true;
    seen[static_cast<std::size_t>(via)] = true;
    std::vector<int> edge{via};
    while (!edge.empty())
    {
        std::vector<int> next;
        for (const int at : edge)
        {
            const Room& room = sRooms[static_cast<std::size_t>(at)];
            for (std::size_t i = 0; i < room.holes.size(); ++i)
            {
                const Hole& hole = room.holes[i];
                if (hole.opened && !hole.cleared)
                    found.push_back(Link{at, static_cast<int>(i)});
                if (hole.to.room >= 0 && hole.to.room < static_cast<int>(sRooms.size()) &&
                    !seen[static_cast<std::size_t>(hole.to.room)])
                {
                    seen[static_cast<std::size_t>(hole.to.room)] = true;
                    next.push_back(hole.to.room);
                }
            }
        }
        edge = std::move(next);
    }
    return found;
}

// What one of this floor's holes is carrying, if anything: everything unfinished beyond it.
std::vector<Link> queueBeyond(int room, int hole)
{
    return queueThrough(room, beyond(room, hole));
}

// START THE FLOOR: one hole per hole, in hole order, so a slot index IS a hole index.
//
// A hole runs its OWN program until it is spent. A SPENT hole is a passage, and a passage runs
// the nearest thing still unfinished beyond it -- at that hole's own depth and out of what is
// left of that hole's own program. It carries them in TURN, nearest first, never as a merged
// blob, and a hole that is carrying nothing simply sits there being a way through.
void beginFloor(int roomId)
{
    Room& room = sRooms[static_cast<std::size_t>(roomId)];
    std::vector<swarm::Hole> holes;
    std::vector<bool> cleared;
    std::vector<bool> opened;
    std::vector<int> killed;
    sSlots.clear();

    for (std::size_t i = 0; i < room.holes.size() && i < sMouths.size(); ++i)
    {
        const Hole& hole = room.holes[i];
        HoleSlot slot{Link{roomId, static_cast<int>(i)}, static_cast<int>(i), sMouths[i].x,
                      sMouths[i].y};
        std::string kind = hole.kind;
        int depth = room.depth;
        bool isOpen = hole.opened;
        bool isSpent = hole.cleared;
        int taken = hole.killed;

        if (hole.cleared)
        {
            for (const Link& waiting : queueBeyond(roomId, static_cast<int>(i)))
            {
                // ONE HOLE IS CARRIED BY ONE PASSAGE. Two holes on this floor can lead into the
                // same place, and a program delivered through both would drain twice.
                if (std::any_of(
                        sSlots.begin(), sSlots.end(), [&](const HoleSlot& s)
                        { return s.owner.room == waiting.room && s.owner.hole == waiting.hole; }))
                    continue;
                const Room& other = sRooms[static_cast<std::size_t>(waiting.room)];
                const Hole& carried = other.holes[static_cast<std::size_t>(waiting.hole)];
                slot.owner = waiting;
                kind = carried.kind;
                depth = other.depth;
                isOpen = true; // opened wherever it is; the passage only carries it
                isSpent = false;
                taken = carried.killed;
                poe::log().info("descent: {} carries {} (depth {})",
                                poeTag(roomId, static_cast<int>(i)),
                                poeTag(waiting.room, waiting.hole), other.depth);
                break;
            }
        }

        holes.push_back(swarm::Hole{slot.x, slot.y, kind, depth});
        cleared.push_back(isSpent);
        opened.push_back(isOpen);
        killed.push_back(taken);
        sSlots.push_back(slot);
    }

    swarm::begin("config/swarm.json", holes, room.depth, cleared, killed, opened);
    // The whole table, once, where a floor starts: what is running here and whose it is.
    // Anything reading wrong on the HUD is readable here first.
    const auto carried = static_cast<std::size_t>(std::count_if(
        sSlots.begin(), sSlots.end(), [&](const HoleSlot& s) { return s.owner.room != roomId; }));
    poe::log().info("descent: {} running {} hole(s) -- {} own, {} carried", room.label,
                    sSlots.size(), sSlots.size() - carried, carried);
}

} // namespace

float holeReach()
{
    // Read once, on the first hole that asks -- after boot has found the source tree, and never
    // again per hole.
    static const float reach = []
    {
        std::ifstream in("config/swarm.json");
        const nlohmann::json j =
            in ? nlohmann::json::parse(in, nullptr, /*allow_exceptions=*/false) : nlohmann::json{};
        if (j.is_discarded() || !j.is_object())
            return 18.0f;
        return j.value("holes", nlohmann::json::object()).value("reach", 18.0f);
    }();
    return reach;
}

std::vector<Room> snapshot()
{
    std::vector<Room> out;
    out.reserve(sRooms.size());
    for (const auto& room : sRooms)
        out.push_back(room);
    return out;
}

int standing()
{
    return sCurrent;
}

void restore(const std::vector<Room>& floors)
{
    forgetSettledArea();
    sRooms.clear();
    sRooms.reserve(floors.size());
    for (const auto& saved : floors)
    {
        Room room = saved;
        // A save written before floors carried tags brings them back untagged. Name them here,
        // in the order they were dug, which is the order they would have been named in --
        // assigned BEFORE the push so each one counts only the rooms that came before it.
        if (room.label.empty())
            room.label = labelFor(room.depth);
        // And one written before the fields were separated brings back B2A where the game now
        // says B2-A. The room KEEPS its letter -- a tag may change shape when the format does,
        // but a room must never change which room it is.
        else if (room.label.find('-') == std::string::npos)
        {
            // Past the leading B, then past the digits: the first non-digit is where the room
            // begins. Scanning for "not one of B0123456789" instead would walk straight over
            // room B -- the letter is in the set it is looking past.
            const auto letter = room.label.find_first_not_of("0123456789", 1);
            if (letter != std::string::npos)
                room.label.insert(letter, "-");
        }
        sRooms.push_back(std::move(room));
    }
    sCurrent = -1; // nowhere until he is stood somewhere
}

bool stand(Engine& engine, EntityManager& em, int room)
{
    if (room < 0 || room >= static_cast<int>(sRooms.size()))
        return false;
    // At the floor's own way in, the same as arriving: what it was mid-fight is
    // not kept, and its unfinished holes muster again.
    return enterNode(engine, em, room, /*atHole=*/-1);
}

void reset()
{
    sRooms.clear();
    sCurrent = -1;
    forgetSettledArea();
}

void leave()
{
    sCurrent = -1;
}

bool descends(int room, int hole)
{
    if (room < 0 || room >= static_cast<int>(sRooms.size()) || hole < 0)
        return false;
    const auto& holes = sRooms[static_cast<std::size_t>(room)].holes;
    if (hole >= static_cast<int>(holes.size()))
        return false;
    // A HOLE IN A WALL IS A RUN THROUGH A CAVITY, not a way underneath: it opens a room at the
    // same depth. Depth is therefore governed entirely by holes in the ground, which is what
    // makes being locked into one kind of trail impossible rather than merely unlikely.
    return !holes::onWall(holes[static_cast<std::size_t>(hole)].kind);
}

// THE FLOOR ON THE FAR SIDE OF A HOLE: the one already there, the act's shared floor where the
// branches rejoin, or a new one dug now. Its DEPTH is the only thing the hole's direction
// decides -- a hole in the ground opens the floor below, a hole in a wall another room on this
// one -- and everything downstream reads the depth rather than the direction.
int openBeyond(int at, int hole, world::Slab slab)
{
    const auto cur = static_cast<std::size_t>(at);
    const int existing = sRooms[cur].holes[static_cast<std::size_t>(hole)].to.room;
    if (existing >= 0)
        return existing;
    const bool down = descends(at, hole);
    const int depth = sRooms[cur].depth + (down ? 1 : 0);
    // At an act boundary every hole DESCENDING into it opens the same floor: the branches
    // rejoin, and the man who explored three of them and the man who took one arrive at the
    // same door. A room reached sideways is not an arrival into the act and never rejoins.
    if (down && convergesAt(depth))
        if (const int shared = sharedAt(depth); shared >= 0)
        {
            poe::log().info("descent: {} rejoins {}", poeTag(at, hole), roomLabel(shared));
            return shared;
        }
    Room fresh;
    fresh.depth = depth;
    fresh.label = labelFor(depth);
    fresh.way_in = 0;
    // WHAT KIND OF SPACE IT IS comes from the hole: a gnawed gap opens a warren. A hole naming
    // none opens the descent's default, which is what a crack in a foundation should do.
    const std::string& opens =
        holes::facts(sRooms[cur].holes[static_cast<std::size_t>(hole)].kind).opens;
    fresh.type = opens.empty() ? holes::defaultType() : opens;
    // A hole cut into a slab opens a POCKET: the same kind of space, shaped like the slab, and
    // grown no further sideways. Only sideways -- a pocket in the ground still goes down.
    if (!down)
    {
        fresh.slab_cols = slab.cols;
        fresh.slab_rows = slab.rows;
    }
    // Said out loud, because a pocket is otherwise indistinguishable in the record from any
    // other room and the difference is the whole reason it exists.
    if (fresh.slab_cols > 0)
        poe::log().info("descent: {} opens a POCKET -- cut into a slab {}x{} tiles",
                        poeTag(at, hole), fresh.slab_cols, fresh.slab_rows);
    // The way in is the FAR END OF THE HOLE HE CAME THROUGH -- same kind, so it wears the same
    // art -- and it arrives already spent, because a passage is what a spent hole is.
    // Which WALL it is in is not settled here: arriving decides that, because which hole he
    // took to get here is what it depends on, and at an act boundary that differs between
    // visits. One writer, and it is the arrival.
    Hole back;
    back.kind = sRooms[cur].holes[static_cast<std::size_t>(hole)].kind;
    back.opened = true;
    back.cleared = true;
    fresh.holes.push_back(back);
    // Indexed access on BOTH sides of the push: growing the vector moves every room, and a
    // reference held across it dangles.
    sRooms.push_back(std::move(fresh));
    return static_cast<int>(sRooms.size()) - 1;
}

bool travel(Engine& engine, EntityManager& em, int hole)
{
    if (sCurrent < 0)
        return false;
    const auto cur = static_cast<std::size_t>(sCurrent);
    if (hole < 0 || hole >= static_cast<int>(sRooms[cur].holes.size()) ||
        !sRooms[cur].holes[static_cast<std::size_t>(hole)].cleared)
    {
        poe::log().error("descent: hole {} is not a passage", hole);
        return false;
    }
    // A PASSAGE IN USE CANNOT BE TRAVELLED, whichever way it goes. What he opened over there is
    // coming through this hole, and he does not get to walk past it -- leaving a floor
    // unfinished costs him the way back into it until he has answered for what he started.
    if (!queueBeyond(sCurrent, hole).empty())
    {
        poe::log().info("descent: {} is still delivering -- finish it first",
                        poeTag(sCurrent, hole));
        return false;
    }

    // MEASURED HERE because this is the last moment the room that answers it exists: he is
    // standing in it, and stepping through tears it down.
    const auto slot = static_cast<std::size_t>(hole);
    const world::Slab slab =
        slot < sMouths.size()
            ? world::slabBeyond(em, sMouths[slot].x, sMouths[slot].y, sRooms[cur].holes[slot].side,
                                placement::kArchReach, placement::kSlabCap)
            : world::Slab{};
    const int farId = openBeyond(sCurrent, hole, slab);
    // WHERE HE COMES OUT is the far end of the passage, which the link already names. A floor
    // he has been through before has its own way in pointing somewhere else entirely, and
    // arriving at that instead of at this hole is how the way back gets lost.
    const auto far = static_cast<std::size_t>(farId);
    Hole& here = sRooms[cur].holes[static_cast<std::size_t>(hole)];
    const int arriveAt = here.to.room == farId ? here.to.hole : sRooms[far].way_in;
    here.to = Link{farId, arriveAt};
    // The far end points back at THIS hole only when he is arriving through that floor's way
    // in: at an act boundary several holes lead into one floor, and the way out is whichever
    // way he came. Climbing back out of a floor must not rewrite the way in of the floor above.
    if (arriveAt >= 0 && arriveAt == sRooms[far].way_in)
    {
        Hole& back = sRooms[far].holes[static_cast<std::size_t>(arriveAt)];
        back.to = Link{sCurrent, hole};
        // AND IT FACES BACK THE WAY HE CAME, decided here rather than when the room was first
        // dug -- for the same reason the link above is. At an act boundary several holes lead
        // into one room, so which wall the way out is in depends on which hole he took to get
        // here, and a side settled once at creation would be answering about a different
        // journey.
        back.side = world::opposite(here.side);
    }

    const int fromId = sCurrent;
    if (enterNode(engine, em, farId, arriveAt))
        return true;
    // A floor that cannot build must not strand him in a torn-down world.
    return enterNode(engine, em, fromId, /*atHole=*/-1);
}

void refreshPassages(EntityManager& em)
{
    for (auto [e, site] : em.registry().view<PlacedHole>().each())
        site.in_use = !queueBeyond(sCurrent, site.hole).empty();
}

// One puff above a hole that is doing something: a handful of blobs at different sizes and
// offsets, drifting at slightly different rates. Square particles read as squares when there
// is one of them and as a cloud when there are several overlapping and none of them agree --
// so the variation IS the effect, not decoration on it.
// `out` is the way the hole faces -- where what is inside it comes from. A hole in the ground
// has none and its vapour simply rises.
void reek(EntityManager& em, float x, float y, bool rising, world::Step out)
{
    static std::mt19937 rng(0x5EEDu); // cosmetic only: nothing here is saved or replayed
    std::uniform_real_distribution<float> spread(-5.0f, 5.0f);
    std::uniform_real_distribution<float> lift(-4.0f, 2.0f);
    std::uniform_real_distribution<float> size(3.0f, 7.0f);
    std::uniform_real_distribution<float> vary(-0.06f, 0.06f);
    std::uniform_real_distribution<float> wander(-4.0f, 4.0f);
    std::uniform_real_distribution<float> live(0.85f, 1.35f);
    std::uniform_real_distribution<float> turn(0.0f, 360.0f);

    auto& reg = em.registry();
    sReekSide = 1 - sReekSide;
    // The wander, across whichever way the vapour is going: a column that leans the same way
    // every puff is a jet, and this is meant to be a breath.
    const float wobble = sReekSide == 0 ? feel::current().reek.drift : -feel::current().reek.drift;
    const bool sideways = out.dc != 0;
    const float lean = sideways ? static_cast<float>(out.dc) * feel::current().reek.rise : wobble;
    // IT STARTS AT THE OPENING, which is BACK along the way it comes out. The mouth is the spot
    // he stands on, a little clear of the hole so he is not inside it; the art is on the other
    // side of that. Nudging always upward was a north wall's habit -- for a hole in the wall
    // BELOW him it started a step into the room, and the vapour appeared to come from thin air
    // beside the hole rather than out of it.
    const bool facing = out.dc != 0 || out.dr != 0;
    const float fromX =
        x - (facing ? static_cast<float>(out.dc) * feel::current().reek.mouth : 0.0f);
    const float fromY =
        y - (facing ? static_cast<float>(out.dr) : 1.0f) * feel::current().reek.mouth;
    for (int i = 0; i < kReekBlobs; ++i)
    {
        const float tone = vary(rng);
        const entt::entity blob = spawn::box(em, fromX + spread(rng), fromY + lift(rng), size(rng),
                                             0.52f + tone, 0.60f + tone, 0.30f + tone);
        // Direction says where it is COMING FROM: up out of a hole in the floor, down out of
        // the way he climbed in by.
        // OUT OF THE HOLE, whichever way it faces. A hole in the ground sends its vapour
        // straight up and the way he climbed in by lets it settle back down; a hole in a WALL
        // pushes it into the room, and it lifts as it goes because vapour does.
        const float climb = sideways      ? -feel::current().reek.rise * 0.35f
                            : out.dr != 0 ? static_cast<float>(out.dr) * feel::current().reek.rise
                                          : (rising ? -feel::current().reek.rise
                                                    : feel::current().reek.rise * 0.7f);
        reg.emplace<Velocity>(
            blob, Velocity{lean + wander(rng), climb + (sideways ? wobble : 0.0f) + wander(rng)});
        Particle p;
        p.lifetime = live(rng);
        p.start_scale = 0.45f;
        p.end_scale = 1.6f; // thins out as it goes, the way vapour does
        reg.emplace<Particle>(blob, p);
        auto& spr = reg.get<Sprite>(blob);
        spr.layer = 3;
        // Turned off the grid: axis-aligned squares read as squares however many you stack,
        // and squares at odd angles overlap into something lumpy.
        spr.rotation = turn(rng);
    }
}

// WHAT EACH HOLE HAS LOST GOES TO THE FLOOR THAT OWNS IT -- a passage's kills belong to the
// floor on the far side, not to the one he is standing on. Taken every frame rather than at
// some exit, because quitting is an exit nobody gets to run code on.
void creditKills()
{
    const std::vector<int>& lost = swarm::progress();
    for (std::size_t i = 0; i < sSlots.size() && i < lost.size(); ++i)
    {
        const Link owner = sSlots[i].owner;
        if (owner.room < 0 || owner.room >= static_cast<int>(sRooms.size()))
            continue;
        auto& holes = sRooms[static_cast<std::size_t>(owner.room)].holes;
        if (static_cast<std::size_t>(owner.hole) < holes.size())
            holes[static_cast<std::size_t>(owner.hole)].killed = lost[i];
    }
}

void spendFinished(EntityManager& em)
{
    // what it carried settles the floor across it, not the one underfoot.
    for (std::size_t s = 0; s < sSlots.size(); ++s)
    {
        const HoleSlot& slot = sSlots[s];
        if (slot.owner.room < 0 || !swarm::holeCleared(em, static_cast<int>(s)))
            continue;
        auto& holes = sRooms[static_cast<std::size_t>(slot.owner.room)].holes;
        const auto oh = static_cast<std::size_t>(slot.owner.hole);
        if (oh >= holes.size() || holes[oh].cleared)
            continue;
        holes[oh].cleared = true;
        if (slot.owner.room != sCurrent)
        {
            // A passage finished what it was carrying; the next thing beyond takes its place.
            poe::log().info("descent: {} spent through a passage",
                            poeTag(slot.owner.room, slot.owner.hole));
            continue;
        }
        // Spent: from spawner to passage. The hole's OWN art carries the component, since a
        // second sprite would stack and fight.
        for (auto [e, placed] : em.registry().view<PlacedHole>().each())
            if (placed.hole == slot.owner.hole &&
                static_cast<std::size_t>(placed.hole) < sMouths.size())
            {
                // It becomes offerable; it does not become a different hole. Only the reach it
                // did not have before is added.
                placed.reach = holeReach();
            }
        poe::log().info("descent: {} spent -- a way through now",
                        poeTag(sCurrent, slot.owner.hole));
    }
}

void breathe(EntityManager& em, const Room& room, float dt)
{
    // finished, or a passage with something coming through it. A puff rises out of a hole in
    // the ground and settles out of one in a wall, which is the difference said in the world.
    sReekTimer += dt;
    if (sReekTimer >= feel::current().reek.every)
    {
        sReekTimer = 0.0f;
        for (std::size_t i = 0; i < room.holes.size() && i < sMouths.size(); ++i)
        {
            const Hole& hole = room.holes[i];
            const bool working =
                hole.cleared ? !queueBeyond(sCurrent, static_cast<int>(i)).empty() : hole.opened;
            if (working)
                // A hole in a wall breathes OUT of itself, which is back the way its wall lies.
                // One in the ground has no way to face and simply rises.
                reek(em, sMouths[i].x, sMouths[i].y,
                     /*rising=*/descends(sCurrent, static_cast<int>(i)),
                     descends(sCurrent, static_cast<int>(i))
                         ? world::Step{}
                         : world::stepOf(world::opposite(hole.side)));
        }
    }
}

void repointPassages(const Room& room)
{
    // finishes -- or when he opens something new over there -- the passage picks up whatever is
    // next, without disturbing the floor he is standing on.
    for (std::size_t s = 0; s < sSlots.size(); ++s)
    {
        HoleSlot& slot = sSlots[s];
        if (slot.at < 0 || static_cast<std::size_t>(slot.at) >= room.holes.size() ||
            !room.holes[static_cast<std::size_t>(slot.at)].cleared)
            continue; // not a passage: it is running its own program, or nothing
        const std::vector<Link> queue = queueBeyond(sCurrent, slot.at);
        Link head;
        for (const Link& waiting : queue)
            if (!std::any_of(sSlots.begin(), sSlots.end(),
                             [&](const HoleSlot& other)
                             {
                                 return &other != &slot && other.owner.room == waiting.room &&
                                        other.owner.hole == waiting.hole;
                             }))
            {
                head = waiting;
                break;
            }
        if (head.room < 0)
        {
            if (slot.owner.room != sCurrent || slot.owner.hole != slot.at)
            {
                swarm::retarget(static_cast<int>(s), swarm::Hole{}, 0);
                slot.owner = Link{sCurrent, slot.at};
            }
            continue;
        }
        if (head.room == slot.owner.room && head.hole == slot.owner.hole)
            continue;
        const Room& other = sRooms[static_cast<std::size_t>(head.room)];
        const Hole& carried = other.holes[static_cast<std::size_t>(head.hole)];
        swarm::retarget(static_cast<int>(s), swarm::Hole{slot.x, slot.y, carried.kind, other.depth},
                        carried.killed);
        poe::log().info("descent: {} now carries {} (depth {})", poeTag(sCurrent, slot.at),
                        poeTag(head.room, head.hole), other.depth);
        slot.owner = head;
    }
}

void update(const Engine& engine, EntityManager& em, float dt)
{
    (void)engine;
    syncArea(em);
    refreshPassages(em);
    creditKills();
    if (sCurrent < 0)
        return;
    const Room& room = sRooms[static_cast<std::size_t>(sCurrent)];
    spendFinished(em);
    breathe(em, room, dt);
    repointPassages(room);
}

int openableUnderfoot(const EntityManager& em, float x, float y)
{
    if (sCurrent < 0)
        return -1;
    const Room& room = sRooms[static_cast<std::size_t>(sCurrent)];
    const float reach = holeReach();
    for (std::size_t i = 0; i < sMouths.size(); ++i)
    {
        if (i < room.holes.size() && room.holes[i].opened)
            continue; // already answered, one way or the other
        const float dx = x - sMouths[i].x;
        const float dy = y - sMouths[i].y;
        if (dx * dx + dy * dy < reach * reach)
            return static_cast<int>(i);
    }
    (void)em;
    return -1;
}

bool open(EntityManager& em, int hole)
{
    if (sCurrent < 0 || hole < 0)
        return false;
    Room& room = sRooms[static_cast<std::size_t>(sCurrent)];
    const auto i = static_cast<std::size_t>(hole);
    if (i >= room.holes.size() || room.holes[i].opened)
        return false;
    room.holes[i].opened = true;
    swarm::wake(hole);
    // The art stops pretending to be floor.
    for (const auto [e, art] : em.registry().view<PlacedHole>().each())
        if (art.hole == hole)
            if (auto* spr = em.registry().try_get<Sprite>(e))
                spr->src_x = art.open_x;
    poe::log().info("descent: hole {} opened -- it answers now", hole);
    return true;
}

int frontsOpen()
{
    if (sCurrent < 0)
        return 0;
    const Room& room = sRooms[static_cast<std::size_t>(sCurrent)];
    int fronts = 0;
    for (std::size_t i = 0; i < room.holes.size(); ++i)
    {
        const Hole& hole = room.holes[i];
        // A hole he broke open and has not finished, or a passage with something coming through
        // it -- both are something he is standing in front of, which is what a front is.
        if (hole.cleared ? !queueBeyond(sCurrent, static_cast<int>(i)).empty() : hole.opened)
            ++fronts;
    }
    return fronts;
}

std::vector<Point> exclusions()
{
    std::vector<Point> out;
    if (sCurrent < 0)
        return out;
    const Room& room = sRooms[static_cast<std::size_t>(sCurrent)];
    // One row per hole, in the floor's own order -- the way he came in included, because a
    // passage carrying something is a front like any other and leaving it off the list would
    // mean the only way to learn it is coming is to be standing there when it arrives.
    for (std::size_t i = 0; i < room.holes.size(); ++i)
    {
        Point point;
        point.tag = poeTag(sCurrent, static_cast<int>(i));
        // The slot IS the hole: a hole that is carrying reads as working even though its own
        // program is long spent, because what it is doing is what arrives out of it.
        const bool running = i < sSlots.size() && sSlots[i].owner.room >= 0 &&
                             !swarm::holeSealed(static_cast<int>(i)) &&
                             !sRooms[static_cast<std::size_t>(sSlots[i].owner.room)]
                                  .holes[static_cast<std::size_t>(sSlots[i].owner.hole)]
                                  .cleared;
        if (running)
        {
            point.state = PointState::Working;
            point.wave = swarm::holeWave(static_cast<int>(i));
            point.waves = swarm::holeWaves(static_cast<int>(i));
        }
        else if (room.holes[i].cleared)
            point.state = PointState::Cleared;
        out.push_back(std::move(point));
    }
    return out;
}

std::string roomLabel(int room)
{
    if (room < 0 || room >= static_cast<int>(sRooms.size()))
        return {};
    return sRooms[static_cast<std::size_t>(room)].label;
}

std::string poeTag(int room, int hole)
{
    const std::string floor = roomLabel(room);
    if (floor.empty())
        return floor;
    // 0 IS THE WAY IN -- the passage he arrived by. It is a point of entry like any other the
    // moment something comes through it, so it is numbered like one; the holes he finds on the
    // floor itself run from 1. Unpadded: a floor holds a handful of holes, and a leading zero
    // is a convention for sorting lists that do not exist here.
    return floor + "-" + std::to_string(hole + 1);
}

std::string hereLabel()
{
    return roomLabel(sCurrent);
}

std::string beyondLabel(int hole)
{
    if (sCurrent < 0)
        return {};
    const Room& room = sRooms[static_cast<std::size_t>(sCurrent)];
    // A place he has been has a name. One he has not is a QUESTION, and saying so is the point:
    // walking back into a floor he cleared is walking, and opening one he has never seen is a
    // commitment. Naming it in advance would flatten the difference.
    const int there = beyond(sCurrent, hole);
    if (there >= 0)
        return roomLabel(there);
    // Except where the branches rejoin: at an act boundary every hole DESCENDING into it opens
    // the same floor, so one he has not dug yet still leads somewhere he has been. The link is
    // only made when he digs, but the destination is known before he does, and calling a place
    // he has walked through a question would be a lie.
    if (descends(sCurrent, hole) && convergesAt(room.depth + 1))
        if (const int shared = sharedAt(room.depth + 1); shared >= 0)
            return roomLabel(shared);
    return std::string{"?"};
}

int stepDir(int hole)
{
    if (sCurrent < 0)
        return 0;
    // From the DEPTHS themselves, so a hole in a wall marks itself as across without anything
    // here having to know what a wall is.
    const Room& room = sRooms[static_cast<std::size_t>(sCurrent)];
    const int there = beyond(sCurrent, hole);
    const int depth = there >= 0 ? sRooms[static_cast<std::size_t>(there)].depth
                                 : room.depth + (descends(sCurrent, hole) ? 1 : 0);
    return depth > room.depth ? 1 : depth < room.depth ? -1 : 0;
}

bool convergesAt(int depth)
{
    const int every = actEvery();
    return every > 0 && depth > 0 && depth % every == 0;
}

bool roomHasWork()
{
    return workUnderway(sCurrent);
}

} // namespace descent
