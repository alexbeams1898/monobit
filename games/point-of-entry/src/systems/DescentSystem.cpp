#include "systems/DescentSystem.h"

#include "Engine.h"
#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "ecs/GameComponents.h"
#include "formats/FloorGen.h"
#include "formats/FloorTypes.h"
#include "formats/SpriteDefLoader.h"
#include "ops/LogUtils.h"
#include "ops/NavUtils.h"
#include "ops/SpawnUtils.h"
#include "systems/PlayerSystem.h"
#include "systems/TileMapRenderer.h"
#include "systems/TravelSystem.h"
#include "systems/WaveSystem.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <random>
#include <unordered_map>
#include <vector>

#include <entt/entt.hpp>

namespace descent
{
namespace
{

constexpr float kPoeSize = 32.0f;

// One dug floor. The seed rebuilds its exact layout; `cleared` is the state
// worth keeping between visits.
// A floor's SPACE comes from one of two places and nothing else about it
// differs: an authored level, or a seed that rebuilds the same generated layout
// every visit. What persists is the Floor; the rest is rebuilt from it, which
// is why it lives out here rather than in the saved half.
struct Node
{
    Floor floor;
    std::vector<swarm::Seep> seeps; // rebuilt every arrival, same every time
};

std::vector<Node> sNodes;

// ONE RECORD PER SEEP THE SWARM IS RUNNING, in the swarm's own order -- which is hole order,
// one slot per hole of the floor he stands on. `owner` is whose program it is: this floor's own
// hole, or a hole on the far side of the passage that this hole has become. Asking the slot is
// the ONLY way to tell the two apart. Working it out three ways is how this went wrong before:
// two parallel arrays tied together by an index offset, and a heuristic comparing node ids.
struct SeepSlot
{
    Link owner;  // whose program is running here
    int at = -1; // which of THIS floor's holes it arrives at
    float x = 0.0f;
    float y = 0.0f;
};
std::vector<SeepSlot> sSlots;
int sCurrent = -1; // node the player stands in; -1 = not in the dig

// IS ANYTHING ACTUALLY RUNNING on this floor -- a hole he broke open and did not
// finish? Not "does it have holes left": a floor nobody has disturbed is quiet by
// definition, because a sealed hole sends nothing. One definition, asked of the
// floor he stands on (is this work?) and of the floor below a hole (is that hole
// leaking?), so the two can never disagree about what an unfinished floor is.
bool workUnderway(int nodeId)
{
    if (nodeId < 0 || nodeId >= static_cast<int>(sNodes.size()))
        return false;
    for (const Hole& hole : sNodes[static_cast<std::size_t>(nodeId)].floor.holes)
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
int beyond(int node, int hole)
{
    if (node < 0 || node >= static_cast<int>(sNodes.size()) || hole < 0)
        return -1;
    const auto& holes = sNodes[static_cast<std::size_t>(node)].floor.holes;
    return hole < static_cast<int>(holes.size()) ? holes[static_cast<std::size_t>(hole)].to.node
                                                 : -1;
}

// What a marker becomes: its kind's look and where creatures surface. Rolled
// from the floor's seed, so a layout is the same holes every time.
struct SeepKind
{
    std::string path;
    int weight = 1;
    sprite_def::Def def;
    bool on_wall = false;
    std::string opens;          // the floor type on the far side; empty = the descent's default
    std::string first_creature; // the vein's face, for the leak preview
};

SeepKind loadKind(const std::string& path)
{
    SeepKind kind;
    kind.path = path;
    std::ifstream sf(path);
    const nlohmann::json sj =
        sf ? nlohmann::json::parse(sf, nullptr, /*allow_exceptions=*/false) : nlohmann::json{};
    if (!sj.is_discarded() && sj.is_object())
    {
        kind.def = sprite_def::load(sj.value("sprite", std::string{}));
        kind.on_wall = sj.value("placement", std::string{"floor"}) == "wall";
        kind.opens = sj.value("opens", std::string{});
        const auto& creatures = sj.value("creatures", nlohmann::json::array());
        if (!creatures.empty())
            kind.first_creature = creatures.front().value("creature", std::string{});
    }
    return kind;
}

// WHERE A KIND OF HOLE SITS AND WHAT IT OPENS. Cached by path: which way a hole goes is asked
// every frame by the prompt, and a per-frame question must not reopen a config file to answer.
const SeepKind& kindFacts(const std::string& path)
{
    static std::unordered_map<std::string, SeepKind> cache;
    const auto known = cache.find(path);
    return known != cache.end() ? known->second : cache.emplace(path, loadKind(path)).first->second;
}

bool kindOnWall(const std::string& path)
{
    return kindFacts(path).on_wall;
}

// THE DESCENT'S DEFAULT SPACE, for a hole that names none: the house's own foundation.
const std::string& defaultType()
{
    static const std::string type = []
    {
        std::ifstream in("config/floor.json");
        const nlohmann::json j =
            in ? nlohmann::json::parse(in, nullptr, /*allow_exceptions=*/false) : nlohmann::json{};
        return j.is_discarded() || !j.is_object()
                   ? std::string{"config/floors/cellar.json"}
                   : j.value("default", std::string{"config/floors/cellar.json"});
    }();
    return type;
}

// WHICH KIND OF SPACE A FLOOR IS, with the fallback applied once here so nothing downstream has
// to remember that an empty string means the default.
const std::string& typeOf(const Node& node)
{
    return node.floor.type.empty() ? defaultType() : node.floor.type;
}

// This kind of space's mix of hole kinds, plus any letter-pinned kinds. Read through the type's
// base chain, so a type that does not name a mix inherits the one it varies from.
// What a KIND OF SPACE imposes on its holes beyond the mix itself.
struct Rules
{
    // How many rooms a network of wall holes grows to before its connections start leading back
    // into rooms it already has. THIS is what bounds a run sideways: the network CLOSES rather
    // than stopping, so nothing ever has to refuse a hole that already looks like a passage.
    int lateral_rooms =
        2; // how far a run of wall holes may carry before this space stops growing them
    int wall_depth = 2; // tiles of solid a wall needs behind it before a hole may be gnawed through
};

Rules loadKindTable(const std::string& typePath, std::vector<SeepKind>& kinds,
                    std::unordered_map<char, std::string>& pinned)
{
    Rules rules;
    const nlohmann::json j = formats::read(typePath);
    if (!j.is_object())
        return rules;
    for (const auto& entry : j.value("seep_types", nlohmann::json::array()))
    {
        SeepKind kind = loadKind(entry.value("seep", std::string{}));
        kind.weight = entry.value("weight", 1);
        kinds.push_back(std::move(kind));
    }
    const nlohmann::json ms = j.value("marker_seeps", nlohmann::json::object());
    for (const auto& [letter, path] : ms.items())
        if (!letter.empty() && path.is_string())
            pinned.emplace(letter.front(), path.get<std::string>());
    rules.lateral_rooms = j.value("lateral_rooms", rules.lateral_rooms);
    rules.wall_depth = j.value("wall_depth", rules.wall_depth);
    return rules;
}

const SeepKind* kindByPath(std::vector<SeepKind>& kinds, const std::string& path)
{
    for (const auto& k : kinds)
        if (k.path == path)
            return &k;
    kinds.push_back(loadKind(path));
    return &kinds.back();
}

// A wall-placed hole arches into the wall above its marker. Both questions such a hole asks come
// from one place: whether the kind may be ASSIGNED to a marker at all, and where its art sits.
// Answering them separately is how art ends up arching into something the assignment rule never
// approved.
constexpr int kArchReach = 4; // tiles above a marker an arch may reach for

float archWallY(const EntityManager& em, float x, float y, int depth)
{
    return world::wallFaceAbove(em, x, y, kArchReach, depth);
}

// Place one hole's art (wall kinds snap their arch to the nearest front wall,
// spawn moving WITH the art) and return where its creatures surface.
// A hole's two faces, read from the art by TAG. Missing tags are loud: a hole whose art cannot
// say which frame is closed would sit there looking open and never react to being opened.
SeepArt artOf(const SeepKind* kind, int index)
{
    SeepArt art;
    art.hole = index;
    if (kind == nullptr || !kind->def.ok)
        return art;
    const int closed = sprite_def::frameOf(kind->def, "closed");
    const int open = sprite_def::frameOf(kind->def, "opened");
    if (closed < 0 || open < 0)
    {
        poe::log().error("descent: '{}' has no closed/opened tags -- it cannot be broken open",
                         kind->def.sheet);
        return art;
    }
    art.closed_x = closed * kind->def.frame_w;
    art.open_x = open * kind->def.frame_w;
    return art;
}

void placeHole(EntityManager& em, float mx, float my, const SeepKind* kind, int index,
               int wallDepth, float& seepX, float& seepY)
{
    const entt::entity hole = spawn::box(em, mx, my, kPoeSize, 0.75f, 0.15f, 0.15f);
    em.registry().emplace<SeepArt>(hole, artOf(kind, index));
    seepX = mx;
    seepY = my;
    if (kind == nullptr || !kind->def.ok)
        return;
    auto& spr = em.registry().get<Sprite>(hole);
    spr.texture_path = kind->def.sheet;
    spr.src_x = em.registry().get<SeepArt>(hole).closed_x; // sealed until he opens it
    spr.src_w = kind->def.frame_w;
    spr.src_h = kind->def.frame_h;
    spr.layer = 1;
    em.registry().remove<SolidColor>(hole);
    if (!kind->on_wall)
        return;
    const float wallBottom = archWallY(em, mx, my, wallDepth);
    if (wallBottom < 0.0f)
        return; // nothing worth arching into; it stays where the marker put it
    const auto ts = static_cast<float>(em.tile_map.tile_size);
    auto& t = em.registry().get<Transform>(hole);
    t.x = std::floor(mx / ts) * ts + ts * 0.5f;
    t.y = wallBottom - static_cast<float>(kind->def.frame_h) * 0.5f;
    em.registry().get<Sprite>(hole).layer = 2;
    seepX = t.x;
    seepY = wallBottom + 8.0f; // the floor at the arch's mouth
}

// Build a node's world into `em`: everything except the player, who is
// preserved and placed by the caller. GL-free. Reports the floor's way in
// and where its ascend spot settled.
// What a floor's holes look like and carry when he walks back into it. A hole he already
// broke open wears its open face -- placement draws every hole sealed, because that is what a
// hole is until something happens to it, and this is the something. A hole that is already
// spent comes back as a way down rather than a spawner: the hole's OWN art carries the
// component, since a second sprite would stack and fight.
// Defined below, beside the queue it reads: arriving on a floor needs it, and what it needs
// to know about the floors beneath is the last thing this file works out.
void beginFloor(int nodeId);

// IT REEKS WHEN IT IS WORKING. A hole he has opened and not finished, or a passage carrying
// something up from below, gives off a slow rise of vapour -- and one that has nothing to send
// gives off none, which is the same thing the Descend prompt is saying, said in the world
// instead of in words. Puffs alternate their drift so the column wanders as it climbs.
constexpr float kReekEvery = 0.5f;
constexpr float kReekRise = 24.0f;
constexpr float kReekDrift = 9.0f;
constexpr int kReekBlobs = 5; // a cloud is a LUMP: several offset blobs read as one soft mass
float sReekTimer = 0.0f;
int sReekSide = 0;

void restoreSpentHoles(EntityManager& em, int nodeId)
{
    Node& node = sNodes[static_cast<std::size_t>(nodeId)];
    auto& reg = em.registry();
    for (const auto [e, art] : reg.view<SeepArt>().each())
    {
        const auto i = static_cast<std::size_t>(art.hole);
        if (art.hole < 0 || i >= node.floor.holes.size() || i >= node.seeps.size())
            continue;
        const Hole& hole = node.floor.holes[i];
        if (hole.opened)
            if (auto* spr = reg.try_get<Sprite>(e))
                spr->src_x = art.open_x;
        if (!hole.cleared || reg.all_of<PassageSite>(e))
            continue;
        PassageSite site;
        site.radius = siteFeel().reach;
        site.hole = art.hole;
        site.spawn_x = node.seeps[i].x;
        site.spawn_y = node.seeps[i].y;
        reg.emplace<PassageSite>(e, site);
    }
}

// EVERY act_every-th depth is one floor that every branch leads into. 0 disables it, and the
// descent goes back to widening forever.
int actEvery()
{
    static const int every = []
    {
        std::ifstream in("config/floor.json");
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
    for (std::size_t i = 0; i < sNodes.size(); ++i)
        if (sNodes[i].floor.depth == depth && sNodes[i].floor.area.empty())
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
    for (const auto& node : sNodes)
        if (node.floor.depth == depth)
            ++rooms;
    return "B" + std::to_string(depth) + "-" + roomLetter(rooms);
}

int indexOfArea(const std::string& area)
{
    for (std::size_t i = 0; i < sNodes.size(); ++i)
        if (sNodes[i].floor.area == area)
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
    for (const auto e : reg.view<AuthoredSeep, Transform>())
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
        Node node;
        node.floor.area = area;
        node.floor.label = labelFor(node.floor.depth);
        sNodes.push_back(std::move(node));
        id = static_cast<int>(sNodes.size()) - 1;
    }
    Node& node = sNodes[static_cast<std::size_t>(id)];
    const std::vector<Hole> kept = node.floor.holes;
    node.seeps.clear();
    node.floor.holes.clear();
    for (std::size_t i = 0; i < holes.size(); ++i)
    {
        const auto& at = reg.get<Transform>(holes[i]);
        const SeepKind kind = loadKind(reg.get<AuthoredSeep>(holes[i]).kind);
        // A hole wears its KIND's face wherever it is: the map says where one is and what sort,
        // never what it looks like.
        const SeepArt art = artOf(&kind, static_cast<int>(i));
        if (kind.def.ok)
        {
            auto& spr = reg.get<Sprite>(holes[i]);
            spr.texture_path = kind.def.sheet;
            spr.src_x = art.closed_x;
            spr.src_w = kind.def.frame_w;
            spr.src_h = kind.def.frame_h;
            spr.layer = 1;
            reg.remove<SolidColor>(holes[i]);
        }
        node.seeps.push_back(swarm::Seep{at.x, at.y, kind.path});
        node.floor.holes.push_back(Hole{Link{}, kind.path, false, false, 0});
        reg.emplace_or_replace<SeepArt>(holes[i], art);
    }
    // An authored floor keeps whatever state the save brought back for holes the map still has.
    for (std::size_t i = 0; i < node.floor.holes.size() && i < kept.size(); ++i)
    {
        node.floor.holes[i].to = kept[i].to;
        node.floor.holes[i].opened = kept[i].opened;
        node.floor.holes[i].cleared = kept[i].cleared;
        node.floor.holes[i].killed = kept[i].killed;
    }

    restoreSpentHoles(em, id);
    beginFloor(id);
    poe::log().info("descent: '{}' is a floor -- {} hole(s) at depth {}", area, node.seeps.size(),
                    node.floor.depth);
    return id;
}

// The floor he is standing in follows the world. Generated space is whatever travelling a
// passage put him in; an authored level is a floor when it has holes and is not one when it
// does not. An authored hole wearing no number yet is how a freshly built level announces
// itself, so this needs no event to listen for.
void syncArea(EntityManager& em)
{
    const std::string& area = travel::currentArea();
    if (area.empty())
        return;
    auto& reg = em.registry();
    if (reg.view<AuthoredSeep>().empty())
    {
        sCurrent = -1; // a room he is only passing through
        return;
    }
    bool fresh = false;
    for (const auto e : reg.view<AuthoredSeep>())
        if (!reg.all_of<SeepArt>(e))
        {
            fresh = true;
            break;
        }
    sCurrent = fresh ? adoptArea(em, area) : indexOfArea(area);
}

// WHICH KIND OF HOLE A MARKER BECOMES: a pinned letter takes its named kind, anything else
// rolls the floor's weighted mix. A wall kind rolled onto a marker with no wall in reach falls
// back to the table's first floor kind -- deterministically, with no extra roll, so the seed
// still reproduces the floor exactly.
const SeepKind* chooseKind(const EntityManager& em, const floorgen::Marker& m,
                           std::vector<SeepKind>& kinds,
                           const std::unordered_map<char, std::string>& pinned, int totalWeight,
                           std::mt19937& rng, int wallDepth)
{
    const auto pin = pinned.find(m.type);
    // A PIN IS AN INTENT, NOT AN EXEMPTION. An authored letter saying "this spot is a gnawed
    // gap" still needs a wall to gnaw through, so it falls through to the same check a rolled
    // kind faces -- a mouse hole in open floor is not a mouse hole.
    const SeepKind* kind = pin != pinned.end() ? kindByPath(kinds, pin->second)
                                               : (kinds.empty() ? nullptr : &kinds.front());
    if (pin == pinned.end() && totalWeight > 0)
    {
        std::uniform_int_distribution<int> roll(0, totalWeight - 1);
        int ticket = roll(rng);
        for (const auto& k : kinds)
        {
            ticket -= k.weight;
            if (ticket < 0)
            {
                kind = &k;
                break;
            }
        }
    }
    // A wall kind needs a wall to arch into, and needs this run to have somewhere left to go.
    // Either way it falls back to the table's first floor kind -- deterministically, with no
    // extra roll, so the seed still reproduces the floor exactly.
    if (kind != nullptr && kind->on_wall && archWallY(em, m.x, m.y, wallDepth) < 0.0f)
        for (const auto& k : kinds)
            if (!k.on_wall)
                return &k;
    return kind;
}

// THE WAY IN, placed beside the floor's own entrance and drawn as the hole it is the far end
// of. Anything placed by offset must land on floor: a fixed nudge from the entrance can sit
// inside a wall in a tight room.
void placeWayIn(EntityManager& em, Node& node, const floorgen::Floor& floor,
                std::vector<SeepKind>& kinds, int wallDepth)
{
    const SeepKind* kind = kindByPath(kinds, node.floor.holes.front().kind);
    // THE WAY IN IS THE FAR END OF THE HOLE HE CAME THROUGH, so it wears that hole's kind -- and
    // a kind that arches into a wall needs a wall wherever that puts it. The floor's own entrance
    // is chosen for standing room and knows nothing about what is above it, so the spot is
    // searched for rather than nudged to: near the entrance if it can be, at the nearest wall
    // otherwise.
    float ux = floor.spawn_x;
    float uy = floor.spawn_y;
    constexpr float kPoint = 1.0f; // a spot, not a body: standAt fits the man to it on arrival
    const bool sited = kind != nullptr && kind->on_wall
                           ? world::archSpotNear(em, ux, uy, kPoint, kPoint, kArchReach, wallDepth)
                           : world::freeSpotNear(em, ux, uy, kPoint, kPoint);
    if (!sited)
    {
        // A floor with no wall to arch into is a floor that failed to build, but he still has to
        // come out somewhere -- standable beats correct-looking.
        poe::log().error("descent: no wall for the way in on {} -- placing it on open floor",
                         node.floor.label);
        world::freeSpotNear(em, ux, uy, kPoint, kPoint);
    }
    float seepX = ux;
    float seepY = uy;
    placeHole(em, ux, uy, kind, 0, wallDepth, seepX, seepY);
    node.seeps[0] = swarm::Seep{seepX, seepY, node.floor.holes.front().kind};
}

// WHICH KIND A HOLE KEEPS between visits. A FLOOR IS THE SAME PLACE EVERY VISIT: a hole keeps
// the kind it was first given, so retuning the table cannot change what is in a room he has
// already stood in. `rolled` is what this marker would take if it were fresh, which is both the
// answer for a new hole and the replacement for a broken one.
const std::string& keptKind(std::string& held, const std::string& rolled, const EntityManager& em,
                            const floorgen::Marker& m, int wallDepth, const std::string& tag)
{
    // What it may NOT keep is a kind that cannot exist where it stands. A hole in a wall with no
    // wall to it was never a place, it was a fault -- and preserving a fault faithfully means
    // every floor dug before the rule existed keeps a hole drawn in open floor forever. Only
    // that direction is repaired: a floor kind is left alone even where a wall has since become
    // available, because THAT would be the reshuffling the rule above exists to prevent.
    if (!held.empty() && kindOnWall(held) &&
        world::wallFaceAbove(em, m.x, m.y, kArchReach, wallDepth) < 0.0f)
    {
        poe::log().info("descent: {} kept '{}' with no wall to it -- it is '{}' now", tag, held,
                        rolled);
        held.clear();
    }
    if (held.empty())
        held = rolled;
    // Resolved once, AFTER every decision: kindByPath appends to `kinds` for a path it has
    // not seen, and a pointer taken before that append is a pointer into the old buffer.
    return held;
}

bool buildNode(EntityManager& em, int nodeId, float& spawnX, float& spawnY)
{
    Node& node = sNodes[static_cast<std::size_t>(nodeId)];
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

    const floorgen::Floor floor = floorgen::generate(em, typeOf(node), node.floor.seed);
    if (!floor.ok)
    {
        poe::log().error("descent: could not build floor at depth {}", node.floor.depth);
        return false;
    }
    node.floor.seed = floor.seed; // first generation picks; every later visit repeats
    spawnX = floor.spawn_x;
    spawnY = floor.spawn_y;

    // THE WAY HE CAME IN IS HOLE ZERO, placed before the floor's own. It is a hole like the
    // rest -- same art, same mouth, same passage rules -- that simply arrives already spent,
    // which is what a passage is. A floor with nothing above it has none and starts at its own.
    const int offset = node.floor.way_in >= 0 ? 1 : 0;
    node.seeps.assign(static_cast<std::size_t>(offset), swarm::Seep{});

    std::vector<SeepKind> kinds;
    std::unordered_map<char, std::string> pinned;
    const Rules rules = loadKindTable(typeOf(node), kinds, pinned);
    int totalWeight = 0;
    for (const auto& k : kinds)
        totalWeight += k.weight;

    std::mt19937 rng(floor.seed * 2654435761u + 97u);
    int index = offset;
    for (const auto& m : floor.markers)
    {
        if (m.type != 'P' && pinned.find(m.type) == pinned.end())
            continue;
        const SeepKind* kind = chooseKind(em, m, kinds, pinned, totalWeight, rng, rules.wall_depth);
        const auto slot = static_cast<std::size_t>(index);
        if (slot >= node.floor.holes.size())
            node.floor.holes.resize(slot + 1);
        const std::string rolled = kind != nullptr ? kind->path : std::string{};
        // Resolved once, AFTER every decision: kindByPath appends to `kinds` for a path it has
        // not seen, and a pointer taken before that append is a pointer into the old buffer.
        kind = kindByPath(kinds, keptKind(node.floor.holes[slot].kind, rolled, em, m,
                                          rules.wall_depth, poeTag(nodeId, index)));
        float seepX = m.x;
        float seepY = m.y;
        placeHole(em, m.x, m.y, kind, index, rules.wall_depth, seepX, seepY);
        node.seeps.push_back(swarm::Seep{seepX, seepY, node.floor.holes[slot].kind});
        ++index;
    }
    node.floor.holes.resize(static_cast<std::size_t>(index));

    if (offset > 0)
        placeWayIn(em, node, floor, kinds, rules.wall_depth);

    restoreSpentHoles(em, nodeId);
    beginFloor(nodeId);
    return true;
}

// Enter a node: build, upload, and stand the player at (x,y) -- or at the
// floor's own way in when the caller passes atWayIn.
// Arriving on an authored floor is TRAVEL, not generation -- the map already
// holds the space, and the level's own start is where he lands unless the
// caller names the spot he is climbing out at.
// Standing at one of a floor's holes -- climbing out of it, in front of its
// mouth. Asked only AFTER the floor is built: a hole's position is rebuilt on
// arrival, so a floor nobody has walked into this sitting has none yet, and a
// caller that worked one out in advance would be reading a floor that does not
// exist. Returns false when the hole is not one this floor has.
bool standAtHole(EntityManager& em, int nodeId, int hole)
{
    const auto& seeps = sNodes[static_cast<std::size_t>(nodeId)].seeps;
    if (hole < 0 || hole >= static_cast<int>(seeps.size()))
        return false;
    const auto& at = seeps[static_cast<std::size_t>(hole)];
    player::standAt(em, at.x, at.y + 24.0f);
    return true;
}

bool enterAuthored(Engine& engine, EntityManager& em, int nodeId, int atHole)
{
    const std::string area = sNodes[static_cast<std::size_t>(nodeId)].floor.area;
    if (!travel::enter(engine, em, area))
    {
        poe::log().error("descent: could not climb out into '{}'", area);
        return false;
    }
    sCurrent = adoptArea(em, area);
    if (sCurrent < 0)
        return false;
    standAtHole(em, sCurrent, atHole); // otherwise the level's own start stands
    return true;
}

// `atHole` names the hole he arrives at; -1 falls back to the floor's own way in, which is
// what arriving without having come through anything means.
bool enterNode(Engine& engine, EntityManager& em, int nodeId, int atHole)
{
    if (!sNodes[static_cast<std::size_t>(nodeId)].floor.area.empty())
        return enterAuthored(engine, em, nodeId, atHole);
    float sx = 0.0f;
    float sy = 0.0f;
    if (!buildNode(em, nodeId, sx, sy))
        return false;
    TileMapRenderer::upload(em.tile_map, em.tile_config, engine.textureManager());
    em.flow_field.last_player_col = -1;
    em.flow_field.last_player_row = -1;
    // In front of the hole he came out of, never on it, so no prompt greets the landing. A
    // floor with no way in and no named hole falls back to the space's own start.
    if (!standAtHole(em, nodeId, atHole) &&
        !standAtHole(em, nodeId, sNodes[static_cast<std::size_t>(nodeId)].floor.way_in))
        player::standAt(em, sx, sy);
    sCurrent = nodeId;
    const Node& here = sNodes[static_cast<std::size_t>(nodeId)];
    poe::log().info("descent: at {} (depth {}, node {}, seed {})", here.floor.label,
                    here.floor.depth, nodeId, here.floor.seed);
    return true;
}

// THE ROOMS THIS ONE RUNS SIDEWAYS TO, itself included -- the component reached by walking wall
// holes only. Not "every room at this depth": two descents into the same depth each grow their
// own network, and counting by depth would silently merge them into one.
std::vector<int> lateralCluster(int nodeId)
{
    std::vector<int> found;
    if (nodeId < 0 || nodeId >= static_cast<int>(sNodes.size()))
        return found;
    std::vector<bool> seen(sNodes.size(), false);
    seen[static_cast<std::size_t>(nodeId)] = true;
    std::vector<int> edge{nodeId};
    while (!edge.empty())
    {
        const int at = edge.back();
        edge.pop_back();
        found.push_back(at);
        const Floor& floor = sNodes[static_cast<std::size_t>(at)].floor;
        for (std::size_t i = 0; i < floor.holes.size(); ++i)
        {
            const int to = floor.holes[i].to.node;
            if (to < 0 || to >= static_cast<int>(sNodes.size()) ||
                seen[static_cast<std::size_t>(to)] || descends(at, static_cast<int>(i)))
                continue;
            seen[static_cast<std::size_t>(to)] = true;
            edge.push_back(to);
        }
    }
    return found;
}

// A SPENT HOLE IN THE CLUSTER THAT LEADS NOWHERE YET, which is what a new connection binds to.
// Its own floor is excluded: a passage from a room to itself is not a passage.
Link spareHoleIn(const std::vector<int>& cluster, int notThis)
{
    for (const int at : cluster)
    {
        if (at == notThis)
            continue;
        const Floor& floor = sNodes[static_cast<std::size_t>(at)].floor;
        for (std::size_t i = 0; i < floor.holes.size(); ++i)
            if (floor.holes[i].cleared && floor.holes[i].to.node < 0 &&
                !descends(at, static_cast<int>(i)))
                return Link{at, static_cast<int>(i)};
    }
    return Link{};
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
// visited, because the graph is not a tree -- branches rejoin at an act boundary and a pair of
// rooms joined by a wall hole is a genuine cycle. Without it a rejoined floor is counted once
// per branch that reaches it, and a cycle never returns at all.
std::vector<Link> queueThrough(int from, int via)
{
    std::vector<Link> found;
    if (via < 0 || via >= static_cast<int>(sNodes.size()))
        return found;
    std::vector<bool> seen(sNodes.size(), false);
    if (from >= 0 && from < static_cast<int>(sNodes.size()))
        seen[static_cast<std::size_t>(from)] = true;
    seen[static_cast<std::size_t>(via)] = true;
    std::vector<int> edge{via};
    while (!edge.empty())
    {
        std::vector<int> next;
        for (const int at : edge)
        {
            const Node& node = sNodes[static_cast<std::size_t>(at)];
            for (std::size_t i = 0; i < node.floor.holes.size(); ++i)
            {
                const Hole& hole = node.floor.holes[i];
                if (hole.opened && !hole.cleared)
                    found.push_back(Link{at, static_cast<int>(i)});
                if (hole.to.node >= 0 && hole.to.node < static_cast<int>(sNodes.size()) &&
                    !seen[static_cast<std::size_t>(hole.to.node)])
                {
                    seen[static_cast<std::size_t>(hole.to.node)] = true;
                    next.push_back(hole.to.node);
                }
            }
        }
        edge = std::move(next);
    }
    return found;
}

// What one of this floor's holes is carrying, if anything: everything unfinished beyond it.
std::vector<Link> queueBeyond(int node, int hole)
{
    return queueThrough(node, beyond(node, hole));
}

// A NETWORK CLOSES ON ITSELF RATHER THAN GROWING FOREVER. The moment a hole in a wall becomes a
// passage, it either leads somewhere new or leads back into the network it belongs to -- and
// which one is decided HERE, once, so the prompt can name the room before he commits to it.
// A question mark then means new ground rather than merely unknown, which is the difference the
// decision is worth making early for.
//
// It binds only when the network is already as big as its kind of space grows, and only to a
// room holding a spare passage. Where there is nothing to bind to it stays a question and
// travelling digs, because a spent hole leading nowhere is the dead end all of this avoids.
void closeTheLoop(int hole)
{
    if (sCurrent < 0 || hole < 0 || descends(sCurrent, hole))
        return; // a hole in the ground goes down; only the sideways ones form a network
    Floor& floor = sNodes[static_cast<std::size_t>(sCurrent)].floor;
    if (static_cast<std::size_t>(hole) >= floor.holes.size() ||
        floor.holes[static_cast<std::size_t>(hole)].to.node >= 0)
        return; // already bound: whatever it leads to, it has led there since before now

    // THE NETWORK'S SIZE IS A PROPERTY OF THE KIND OF NETWORK, so it comes from the space this
    // hole OPENS rather than the one he happens to be standing in. A cluster holds rooms of more
    // than one kind -- the first room of a warren is whatever he came from -- and reading the
    // standing floor would give the same network two different sizes depending on where he was
    // when it grew.
    std::vector<SeepKind> kinds;
    std::unordered_map<char, std::string> pinned;
    const std::string& opens = kindFacts(floor.holes[static_cast<std::size_t>(hole)].kind).opens;
    const Rules rules = loadKindTable(opens.empty() ? defaultType() : opens, kinds, pinned);
    const std::vector<int> cluster = lateralCluster(sCurrent);
    if (static_cast<int>(cluster.size()) < rules.lateral_rooms)
        return; // room to grow: leave it a question and let travelling dig

    const Link spare = spareHoleIn(cluster, sCurrent);
    if (spare.node < 0)
        return; // a full network with nothing spare still has to lead somewhere

    floor.holes[static_cast<std::size_t>(hole)].to = spare;
    sNodes[static_cast<std::size_t>(spare.node)]
        .floor.holes[static_cast<std::size_t>(spare.hole)]
        .to = Link{sCurrent, hole};
    poe::log().info("descent: {} runs back to {}", poeTag(sCurrent, hole),
                    poeTag(spare.node, spare.hole));
}

// START THE FLOOR: one seep per hole, in hole order, so a slot index IS a hole index.
//
// A hole runs its OWN program until it is spent. A SPENT hole is a passage, and a passage runs
// the nearest thing still unfinished beyond it -- at that hole's own depth and out of what is
// left of that hole's own program. It carries them in TURN, nearest first, never as a merged
// blob, and a hole that is carrying nothing simply sits there being a way through.
void beginFloor(int nodeId)
{
    Node& node = sNodes[static_cast<std::size_t>(nodeId)];
    std::vector<swarm::Seep> seeps;
    std::vector<bool> cleared;
    std::vector<bool> opened;
    std::vector<int> killed;
    sSlots.clear();

    for (std::size_t i = 0; i < node.floor.holes.size() && i < node.seeps.size(); ++i)
    {
        const Hole& hole = node.floor.holes[i];
        SeepSlot slot{Link{nodeId, static_cast<int>(i)}, static_cast<int>(i), node.seeps[i].x,
                      node.seeps[i].y};
        std::string kind = hole.kind;
        int depth = node.floor.depth;
        bool isOpen = hole.opened;
        bool isSpent = hole.cleared;
        int taken = hole.killed;

        if (hole.cleared)
        {
            for (const Link& waiting : queueBeyond(nodeId, static_cast<int>(i)))
            {
                // ONE HOLE IS CARRIED BY ONE PASSAGE. Two holes on this floor can lead into the
                // same place, and a program delivered through both would drain twice.
                if (std::any_of(
                        sSlots.begin(), sSlots.end(), [&](const SeepSlot& s)
                        { return s.owner.node == waiting.node && s.owner.hole == waiting.hole; }))
                    continue;
                const Floor& other = sNodes[static_cast<std::size_t>(waiting.node)].floor;
                const Hole& carried = other.holes[static_cast<std::size_t>(waiting.hole)];
                slot.owner = waiting;
                kind = carried.kind;
                depth = other.depth;
                isOpen = true; // opened wherever it is; the passage only carries it
                isSpent = false;
                taken = carried.killed;
                poe::log().info("descent: {} carries {} (depth {})",
                                poeTag(nodeId, static_cast<int>(i)),
                                poeTag(waiting.node, waiting.hole), other.depth);
                break;
            }
        }

        seeps.push_back(swarm::Seep{slot.x, slot.y, kind, depth});
        cleared.push_back(isSpent);
        opened.push_back(isOpen);
        killed.push_back(taken);
        sSlots.push_back(slot);
    }

    swarm::begin("config/swarm.json", seeps, node.floor.depth, cleared, killed, opened);
    // The whole table, once, where a floor starts: what is running here and whose it is.
    // Anything reading wrong on the HUD is readable here first.
    const auto carried = static_cast<std::size_t>(std::count_if(
        sSlots.begin(), sSlots.end(), [&](const SeepSlot& s) { return s.owner.node != nodeId; }));
    poe::log().info("descent: {} running {} seep(s) -- {} own, {} carried", node.floor.label,
                    sSlots.size(), sSlots.size() - carried, carried);
}

} // namespace

const SiteFeel& siteFeel()
{
    // Read once, on the first hole that asks -- after boot has found the
    // source tree, and never again per site.
    static const SiteFeel feel = []
    {
        SiteFeel f;
        std::ifstream in("config/swarm.json");
        const nlohmann::json j =
            in ? nlohmann::json::parse(in, nullptr, /*allow_exceptions=*/false) : nlohmann::json{};
        if (j.is_discarded() || !j.is_object())
            return f;
        const nlohmann::json site = j.value("sites", nlohmann::json::object());
        f.reach = site.value("reach", f.reach);
        f.leak_interval = site.value("leak_interval", f.leak_interval);
        return f;
    }();
    return feel;
}

std::vector<Floor> snapshot()
{
    std::vector<Floor> out;
    out.reserve(sNodes.size());
    for (const auto& node : sNodes)
        out.push_back(node.floor);
    return out;
}

int standing()
{
    return sCurrent;
}

void restore(const std::vector<Floor>& floors)
{
    sNodes.clear();
    sNodes.reserve(floors.size());
    for (const auto& floor : floors)
    {
        Node node;
        node.floor = floor;
        // A save written before floors carried tags brings them back untagged. Name them here,
        // in the order they were dug, which is the order they would have been named in --
        // assigned BEFORE the push so each one counts only the rooms that came before it.
        if (node.floor.label.empty())
            node.floor.label = labelFor(node.floor.depth);
        // And one written before the fields were separated brings back B2A where the game now
        // says B2-A. The room KEEPS its letter -- a tag may change shape when the format does,
        // but a room must never change which room it is.
        else if (node.floor.label.find('-') == std::string::npos)
        {
            // Past the leading B, then past the digits: the first non-digit is where the room
            // begins. Scanning for "not one of B0123456789" instead would walk straight over
            // room B -- the letter is in the set it is looking past.
            const auto letter = node.floor.label.find_first_not_of("0123456789", 1);
            if (letter != std::string::npos)
                node.floor.label.insert(letter, "-");
        }
        sNodes.push_back(std::move(node));
    }
    sCurrent = -1; // nowhere until he is stood somewhere
}

bool stand(Engine& engine, EntityManager& em, int node)
{
    if (node < 0 || node >= static_cast<int>(sNodes.size()))
        return false;
    // At the floor's own way in, the same as arriving: what it was mid-fight is
    // not kept, and its unfinished holes muster again.
    return enterNode(engine, em, node, /*atHole=*/-1);
}

void reset()
{
    sNodes.clear();
    sCurrent = -1;
}

void leave()
{
    sCurrent = -1;
}

bool descends(int node, int hole)
{
    if (node < 0 || node >= static_cast<int>(sNodes.size()) || hole < 0)
        return false;
    const auto& holes = sNodes[static_cast<std::size_t>(node)].floor.holes;
    if (hole >= static_cast<int>(holes.size()))
        return false;
    // A HOLE IN A WALL IS A RUN THROUGH A CAVITY, not a way underneath: it opens a room at the
    // same depth. Depth is therefore governed entirely by holes in the ground, which is what
    // makes being locked into one kind of trail impossible rather than merely unlikely.
    return !kindOnWall(holes[static_cast<std::size_t>(hole)].kind);
}

// THE FLOOR ON THE FAR SIDE OF A HOLE: the one already there, the act's shared floor where the
// branches rejoin, or a new one dug now. Its DEPTH is the only thing the hole's direction
// decides -- a hole in the ground opens the floor below, a hole in a wall another room on this
// one -- and everything downstream reads the depth rather than the direction.
int openBeyond(int at, int hole)
{
    const auto cur = static_cast<std::size_t>(at);
    const int existing = sNodes[cur].floor.holes[static_cast<std::size_t>(hole)].to.node;
    if (existing >= 0)
        return existing;
    const bool down = descends(at, hole);
    const int depth = sNodes[cur].floor.depth + (down ? 1 : 0);
    // At an act boundary every hole DESCENDING into it opens the same floor: the branches
    // rejoin, and the man who explored three of them and the man who took one arrive at the
    // same door. A room reached sideways is not an arrival into the act and never rejoins.
    if (down && convergesAt(depth))
        if (const int shared = sharedAt(depth); shared >= 0)
        {
            poe::log().info("descent: {} rejoins {}", poeTag(at, hole), floorLabel(shared));
            return shared;
        }
    Node fresh;
    fresh.floor.depth = depth;
    fresh.floor.label = labelFor(depth);
    fresh.floor.way_in = 0;
    // WHAT KIND OF SPACE IT IS comes from the hole: a gnawed gap opens a warren. A hole naming
    // none opens the descent's default, which is what a crack in a foundation should do.
    const std::string& opens =
        kindFacts(sNodes[cur].floor.holes[static_cast<std::size_t>(hole)].kind).opens;
    fresh.floor.type = opens.empty() ? defaultType() : opens;
    // The way in is the FAR END OF THE HOLE HE CAME THROUGH -- same kind, so it wears the same
    // art -- and it arrives already spent, because a passage is what a spent hole is.
    Hole back;
    back.kind = sNodes[cur].floor.holes[static_cast<std::size_t>(hole)].kind;
    back.opened = true;
    back.cleared = true;
    fresh.floor.holes.push_back(back);
    // Indexed access on BOTH sides of the push: growing the vector moves every node, and a
    // reference held across it dangles.
    sNodes.push_back(std::move(fresh));
    return static_cast<int>(sNodes.size()) - 1;
}

bool travel(Engine& engine, EntityManager& em, int hole)
{
    if (sCurrent < 0)
        return false;
    const auto cur = static_cast<std::size_t>(sCurrent);
    if (hole < 0 || hole >= static_cast<int>(sNodes[cur].floor.holes.size()) ||
        !sNodes[cur].floor.holes[static_cast<std::size_t>(hole)].cleared)
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

    const int farId = openBeyond(sCurrent, hole);
    // WHERE HE COMES OUT is the far end of the passage, which the link already names. A floor
    // he has been through before has its own way in pointing somewhere else entirely, and
    // arriving at that instead of at this hole is how the way back gets lost.
    const auto far = static_cast<std::size_t>(farId);
    Hole& here = sNodes[cur].floor.holes[static_cast<std::size_t>(hole)];
    const int arriveAt = here.to.node == farId ? here.to.hole : sNodes[far].floor.way_in;
    here.to = Link{farId, arriveAt};
    // The far end points back at THIS hole only when he is arriving through that floor's way
    // in: at an act boundary several holes lead into one floor, and the way out is whichever
    // way he came. Climbing back out of a floor must not rewrite the way in of the floor above.
    if (arriveAt >= 0 && arriveAt == sNodes[far].floor.way_in)
        sNodes[far].floor.holes[static_cast<std::size_t>(arriveAt)].to = Link{sCurrent, hole};

    const int fromId = sCurrent;
    if (enterNode(engine, em, farId, arriveAt))
        return true;
    // A floor that cannot build must not strand him in a torn-down world.
    return enterNode(engine, em, fromId, /*atHole=*/-1);
}

void refreshLeaks(EntityManager& em)
{
    for (auto [e, site] : em.registry().view<PassageSite>().each())
        site.leaking = !queueBeyond(sCurrent, site.hole).empty();
}

// One puff above a hole that is doing something: a handful of blobs at different sizes and
// offsets, drifting at slightly different rates. Square particles read as squares when there
// is one of them and as a cloud when there are several overlapping and none of them agree --
// so the variation IS the effect, not decoration on it.
void reek(EntityManager& em, float x, float y, bool rising)
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
    const float lean = sReekSide == 0 ? kReekDrift : -kReekDrift;
    for (int i = 0; i < kReekBlobs; ++i)
    {
        const float tone = vary(rng);
        const entt::entity blob = spawn::box(em, x + spread(rng), y - 10.0f + lift(rng), size(rng),
                                             0.52f + tone, 0.60f + tone, 0.30f + tone);
        // Direction says where it is COMING FROM: up out of a hole in the floor, down out of
        // the way he climbed in by.
        const float climb = rising ? -kReekRise : kReekRise * 0.7f;
        reg.emplace<Velocity>(blob, Velocity{lean + wander(rng), climb + wander(rng)});
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

// WHAT EACH SEEP HAS LOST GOES TO THE FLOOR THAT OWNS IT -- a passage's kills belong to the
// floor on the far side, not to the one he is standing on. Taken every frame rather than at
// some exit, because quitting is an exit nobody gets to run code on.
void creditKills()
{
    const std::vector<int>& lost = swarm::progress();
    for (std::size_t i = 0; i < sSlots.size() && i < lost.size(); ++i)
    {
        const Link owner = sSlots[i].owner;
        if (owner.node < 0 || owner.node >= static_cast<int>(sNodes.size()))
            continue;
        auto& holes = sNodes[static_cast<std::size_t>(owner.node)].floor.holes;
        if (static_cast<std::size_t>(owner.hole) < holes.size())
            holes[static_cast<std::size_t>(owner.hole)].killed = lost[i];
    }
}

void spendFinished(EntityManager& em, Node& node)
{
    // what it carried settles the floor across it, not the one underfoot.
    for (std::size_t s = 0; s < sSlots.size(); ++s)
    {
        const SeepSlot& slot = sSlots[s];
        if (slot.owner.node < 0 || !swarm::seepCleared(em, static_cast<int>(s)))
            continue;
        auto& holes = sNodes[static_cast<std::size_t>(slot.owner.node)].floor.holes;
        const auto oh = static_cast<std::size_t>(slot.owner.hole);
        if (oh >= holes.size() || holes[oh].cleared)
            continue;
        holes[oh].cleared = true;
        if (slot.owner.node != sCurrent)
        {
            // A passage finished what it was carrying; the next thing beyond takes its place.
            poe::log().info("descent: {} spent through a passage",
                            poeTag(slot.owner.node, slot.owner.hole));
            continue;
        }
        // Spent: from spawner to passage. The hole's OWN art carries the component, since a
        // second sprite would stack and fight.
        for (const auto [e, art] : em.registry().view<SeepArt>().each())
            if (art.hole == slot.owner.hole &&
                static_cast<std::size_t>(art.hole) < node.seeps.size())
            {
                PassageSite site;
                site.radius = siteFeel().reach;
                site.hole = art.hole;
                site.spawn_x = node.seeps[static_cast<std::size_t>(art.hole)].x;
                site.spawn_y = node.seeps[static_cast<std::size_t>(art.hole)].y;
                em.registry().emplace_or_replace<PassageSite>(e, site);
            }
        closeTheLoop(slot.owner.hole);
        poe::log().info("descent: {} spent -- a way through now",
                        poeTag(sCurrent, slot.owner.hole));
    }
}

void breathe(EntityManager& em, const Node& node, float dt)
{
    // finished, or a passage with something coming through it. A puff rises out of a hole in
    // the ground and settles out of one in a wall, which is the difference said in the world.
    sReekTimer += dt;
    if (sReekTimer >= kReekEvery)
    {
        sReekTimer = 0.0f;
        for (std::size_t i = 0; i < node.floor.holes.size() && i < node.seeps.size(); ++i)
        {
            const Hole& hole = node.floor.holes[i];
            const bool working =
                hole.cleared ? !queueBeyond(sCurrent, static_cast<int>(i)).empty() : hole.opened;
            if (working)
                reek(em, node.seeps[i].x, node.seeps[i].y,
                     /*rising=*/descends(sCurrent, static_cast<int>(i)));
        }
    }
}

void repointPassages(const Node& node)
{
    // finishes -- or when he opens something new over there -- the passage picks up whatever is
    // next, without disturbing the floor he is standing on.
    for (std::size_t s = 0; s < sSlots.size(); ++s)
    {
        SeepSlot& slot = sSlots[s];
        if (slot.at < 0 || static_cast<std::size_t>(slot.at) >= node.floor.holes.size() ||
            !node.floor.holes[static_cast<std::size_t>(slot.at)].cleared)
            continue; // not a passage: it is running its own program, or nothing
        const std::vector<Link> queue = queueBeyond(sCurrent, slot.at);
        Link head;
        for (const Link& waiting : queue)
            if (!std::any_of(sSlots.begin(), sSlots.end(),
                             [&](const SeepSlot& other)
                             {
                                 return &other != &slot && other.owner.node == waiting.node &&
                                        other.owner.hole == waiting.hole;
                             }))
            {
                head = waiting;
                break;
            }
        if (head.node < 0)
        {
            if (slot.owner.node != sCurrent || slot.owner.hole != slot.at)
            {
                swarm::retarget(static_cast<int>(s), swarm::Seep{}, 0);
                slot.owner = Link{sCurrent, slot.at};
            }
            continue;
        }
        if (head.node == slot.owner.node && head.hole == slot.owner.hole)
            continue;
        const Floor& other = sNodes[static_cast<std::size_t>(head.node)].floor;
        const Hole& carried = other.holes[static_cast<std::size_t>(head.hole)];
        swarm::retarget(static_cast<int>(s), swarm::Seep{slot.x, slot.y, carried.kind, other.depth},
                        carried.killed);
        poe::log().info("descent: {} now carries {} (depth {})", poeTag(sCurrent, slot.at),
                        poeTag(head.node, head.hole), other.depth);
        slot.owner = head;
    }
}

void update(Engine& engine, EntityManager& em, float dt)
{
    (void)engine;
    syncArea(em);
    refreshLeaks(em);
    creditKills();
    if (sCurrent < 0)
        return;
    Node& node = sNodes[static_cast<std::size_t>(sCurrent)];
    spendFinished(em, node);
    breathe(em, node, dt);
    repointPassages(node);
}

int openableUnderfoot(const EntityManager& em, float x, float y)
{
    if (sCurrent < 0)
        return -1;
    const Node& node = sNodes[static_cast<std::size_t>(sCurrent)];
    const float reach = siteFeel().reach;
    for (std::size_t i = 0; i < node.seeps.size(); ++i)
    {
        if (i < node.floor.holes.size() && node.floor.holes[i].opened)
            continue; // already answered, one way or the other
        const float dx = x - node.seeps[i].x;
        const float dy = y - node.seeps[i].y;
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
    Node& node = sNodes[static_cast<std::size_t>(sCurrent)];
    const auto i = static_cast<std::size_t>(hole);
    if (i >= node.floor.holes.size() || node.floor.holes[i].opened)
        return false;
    node.floor.holes[i].opened = true;
    swarm::wake(hole);
    // The art stops pretending to be floor.
    for (const auto [e, art] : em.registry().view<SeepArt>().each())
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
    const Node& node = sNodes[static_cast<std::size_t>(sCurrent)];
    int fronts = 0;
    for (std::size_t i = 0; i < node.floor.holes.size(); ++i)
    {
        const Hole& hole = node.floor.holes[i];
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
    const Node& node = sNodes[static_cast<std::size_t>(sCurrent)];
    // One row per hole, in the floor's own order -- the way he came in included, because a
    // passage carrying something is a front like any other and leaving it off the list would
    // mean the only way to learn it is coming is to be standing there when it arrives.
    for (std::size_t i = 0; i < node.floor.holes.size(); ++i)
    {
        Point point;
        point.tag = poeTag(sCurrent, static_cast<int>(i));
        // The slot IS the hole: a hole that is carrying reads as working even though its own
        // program is long spent, because what it is doing is what arrives out of it.
        const bool running = i < sSlots.size() && sSlots[i].owner.node >= 0 &&
                             !swarm::seepSealed(static_cast<int>(i)) &&
                             !sNodes[static_cast<std::size_t>(sSlots[i].owner.node)]
                                  .floor.holes[static_cast<std::size_t>(sSlots[i].owner.hole)]
                                  .cleared;
        if (running)
        {
            point.state = PointState::Working;
            point.wave = swarm::seepWave(static_cast<int>(i));
            point.waves = swarm::seepWaves(static_cast<int>(i));
        }
        else if (node.floor.holes[i].cleared)
            point.state = PointState::Cleared;
        out.push_back(std::move(point));
    }
    return out;
}

std::string floorLabel(int node)
{
    if (node < 0 || node >= static_cast<int>(sNodes.size()))
        return {};
    return sNodes[static_cast<std::size_t>(node)].floor.label;
}

std::string poeTag(int node, int hole)
{
    const std::string floor = floorLabel(node);
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
    return floorLabel(sCurrent);
}

std::string beyondLabel(int hole)
{
    if (sCurrent < 0)
        return {};
    const Node& node = sNodes[static_cast<std::size_t>(sCurrent)];
    // A place he has been has a name. One he has not is a QUESTION, and saying so is the point:
    // walking back into a floor he cleared is walking, and opening one he has never seen is a
    // commitment. Naming it in advance would flatten the difference.
    const int there = beyond(sCurrent, hole);
    if (there >= 0)
        return floorLabel(there);
    // Except where the branches rejoin: at an act boundary every hole DESCENDING into it opens
    // the same floor, so one he has not dug yet still leads somewhere he has been. The link is
    // only made when he digs, but the destination is known before he does, and calling a place
    // he has walked through a question would be a lie.
    if (descends(sCurrent, hole) && convergesAt(node.floor.depth + 1))
        if (const int shared = sharedAt(node.floor.depth + 1); shared >= 0)
            return floorLabel(shared);
    return std::string{"?"};
}

int stepDir(int hole)
{
    if (sCurrent < 0)
        return 0;
    // From the DEPTHS themselves, so a hole in a wall marks itself as across without anything
    // here having to know what a wall is.
    const Node& node = sNodes[static_cast<std::size_t>(sCurrent)];
    const int there = beyond(sCurrent, hole);
    const int depth = there >= 0 ? sNodes[static_cast<std::size_t>(there)].floor.depth
                                 : node.floor.depth + (descends(sCurrent, hole) ? 1 : 0);
    return depth > node.floor.depth ? 1 : depth < node.floor.depth ? -1 : 0;
}

bool convergesAt(int depth)
{
    const int every = actEvery();
    return every > 0 && depth > 0 && depth % every == 0;
}

bool floorHasWork()
{
    return workUnderway(sCurrent);
}

} // namespace descent
