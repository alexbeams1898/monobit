#include "systems/DescentSystem.h"

#include "Engine.h"
#include "FloorGen.h"
#include "SpriteDefLoader.h"
#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "ecs/GameComponents.h"
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

// ONE RECORD PER SEEP THE SWARM IS RUNNING, in the swarm's own order. Everything about a seep
// lives here: whose hole it is, and where it arrives. There is exactly one way to ask whether a
// seep is one of this floor's own holes or a passage carrying another floor's -- ask the slot.
// Three separate ways to work that out is how this went wrong before: two parallel arrays tied
// together by an index offset, and a heuristic comparing node ids on top.
struct Owner
{
    int node = -1;
    int hole = -1;
};

// Where a seep arrives from. A floor's own hole is its own mouth; a passage's mouth is the way
// between floors that the work is coming through.
enum class Mouth
{
    Own,  // a hole of the floor he is standing on
    Down, // a way down, carrying what is unfinished beneath it
    Up    // the way back up, carrying what he left running above
};

struct SeepSlot
{
    Owner owner;
    Mouth mouth = Mouth::Own;
    int hole = -1; // for Down, which of THIS floor's holes the passage is; unused otherwise
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
    const Node& node = sNodes[static_cast<std::size_t>(nodeId)];
    for (std::size_t i = 0; i < node.floor.cleared.size(); ++i)
        if (!node.floor.cleared[i] && i < node.floor.opened.size() && node.floor.opened[i])
            return true;
    return false;
}

// A WAY DOWN LEAKS BECAUSE HE LEFT SOMETHING RUNNING. Not because a floor exists
// below it: an undug floor has nothing coming out of it, and a dug one he never
// broke anything open on has nothing either. What comes up the hole is what he
// disturbed and walked away from -- so a leak is a report on his own unfinished
// business, and a quiet hole means there is nothing down there to answer for.
bool workBehind(int hole)
{
    if (sCurrent < 0 || hole < 0)
        return false;
    const auto& child = sNodes[static_cast<std::size_t>(sCurrent)].floor.child;
    if (hole >= static_cast<int>(child.size()))
        return false;
    return workUnderway(child[static_cast<std::size_t>(hole)]);
}

// What a marker becomes: its kind's look and where creatures surface. Rolled
// from the floor's seed, so a layout is the same holes every time.
struct SeepKind
{
    std::string path;
    int weight = 1;
    sprite_def::Def def;
    bool on_wall = false;
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
        const auto& creatures = sj.value("creatures", nlohmann::json::array());
        if (!creatures.empty())
            kind.first_creature = creatures.front().value("creature", std::string{});
    }
    return kind;
}

// The floor's mix of hole kinds plus any letter-pinned kinds, from floor.json.
void loadKindTable(std::vector<SeepKind>& kinds, std::unordered_map<char, std::string>& pinned)
{
    std::ifstream in("config/floor.json");
    const nlohmann::json j =
        in ? nlohmann::json::parse(in, nullptr, /*allow_exceptions=*/false) : nlohmann::json{};
    if (j.is_discarded() || !j.is_object())
        return;
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
}

const SeepKind* kindByPath(std::vector<SeepKind>& kinds, const std::string& path)
{
    for (const auto& k : kinds)
        if (k.path == path)
            return &k;
    kinds.push_back(loadKind(path));
    return &kinds.back();
}

// Is there a wall within arch-snapping reach above this spot? Wall-placement
// kinds may only be ASSIGNED where this holds -- an arch in open floor is a
// doorway to nothing.
bool wallInReach(const EntityManager& em, float x, float y)
{
    const auto ts = static_cast<float>(em.tile_map.tile_size);
    for (int step = 1; step <= 4; ++step)
        if (!world::walkable(em, x, y - static_cast<float>(step) * ts))
            return true;
    return false;
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

void placeHole(EntityManager& em, const floorgen::Marker& m, const SeepKind* kind, int index,
               float& seepX, float& seepY)
{
    const entt::entity hole = spawn::box(em, m.x, m.y, kPoeSize, 0.75f, 0.15f, 0.15f);
    em.registry().emplace<SeepArt>(hole, artOf(kind, index));
    seepX = m.x;
    seepY = m.y;
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
    const auto ts = static_cast<float>(em.tile_map.tile_size);
    for (int step = 1; step <= 4; ++step)
    {
        const float wy = m.y - static_cast<float>(step) * ts;
        if (!world::walkable(em, m.x, wy))
        {
            auto& t = em.registry().get<Transform>(hole);
            t.x = std::floor(m.x / ts) * ts + ts * 0.5f;
            const float wallBottom = std::floor(wy / ts) * ts + ts;
            t.y = wallBottom - static_cast<float>(kind->def.frame_h) * 0.5f;
            em.registry().get<Sprite>(hole).layer = 2;
            seepX = t.x;
            seepY = wallBottom + 8.0f; // the floor at the arch's mouth
            break;
        }
    }
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
void beginFloor(int nodeId, float upX, float upY);

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
        if (art.hole < 0 || i >= node.floor.opened.size() || !node.floor.opened[i])
            continue;
        if (auto* spr = reg.try_get<Sprite>(e))
            spr->src_x = art.open_x;
    }
    for (const auto [e, art] : reg.view<SeepArt>().each())
    {
        const auto i = static_cast<std::size_t>(art.hole);
        if (art.hole < 0 || i >= node.floor.cleared.size() || !node.floor.cleared[i] ||
            reg.all_of<DescendSite>(e))
            continue;
        DescendSite site;
        site.radius = siteFeel().reach;
        site.hole = art.hole;
        site.spawn_x = node.seeps[i].x;
        site.spawn_y = node.seeps[i].y;
        reg.emplace<DescendSite>(e, site);
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
    node.seeps.clear();
    node.floor.kind.clear();
    for (std::size_t i = 0; i < holes.size(); ++i)
    {
        const auto& at = reg.get<Transform>(holes[i]);
        const SeepKind kind = loadKind(reg.get<AuthoredSeep>(holes[i]).kind);
        // A hole wears its KIND'''s face wherever it is: the map says where one
        // is and what sort, never what it looks like.
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
        node.floor.kind.push_back(kind.path);
        reg.emplace_or_replace<SeepArt>(holes[i], art);
    }
    node.floor.child.resize(node.seeps.size(), -1);
    node.floor.cleared.resize(node.seeps.size(), false);
    node.floor.opened.resize(node.seeps.size(), false);
    node.floor.killed.resize(node.seeps.size(), 0);
    node.floor.kind.resize(node.seeps.size());

    restoreSpentHoles(em, id);
    beginFloor(id, 0.0f, 0.0f);
    poe::log().info("descent: '{}' is a floor -- {} hole(s) at depth {}", area, node.seeps.size(),
                    node.floor.depth);
    return id;
}

// The floor he is standing in follows the world. Generated space is whatever
// descending or ascending put him in; an authored level is a floor when it has holes and
// is not one when it does not. An authored hole wearing no number yet is how a
// freshly built level announces itself, so this needs no event to listen for.
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

bool buildNode(EntityManager& em, int nodeId, float& spawnX, float& spawnY, float& upX, float& upY)
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

    const floorgen::Floor floor =
        floorgen::generate(em, "config/floor.json", "config/rooms", node.floor.seed);
    if (!floor.ok)
    {
        poe::log().error("descent: could not build floor at depth {}", node.floor.depth);
        return false;
    }
    node.floor.seed = floor.seed; // first generation picks; every later visit repeats
    spawnX = floor.spawn_x;
    spawnY = floor.spawn_y;

    std::vector<SeepKind> kinds;
    std::unordered_map<char, std::string> pinned;
    loadKindTable(kinds, pinned);
    int totalWeight = 0;
    for (const auto& k : kinds)
        totalWeight += k.weight;

    node.seeps.clear();
    node.floor.kind.clear();
    std::mt19937 rng(floor.seed * 2654435761u + 97u);
    for (const auto& m : floor.markers)
    {
        const auto pin = pinned.find(m.type);
        if (m.type != 'P' && pin == pinned.end())
            continue;
        const SeepKind* kind = kinds.empty() ? nullptr : &kinds.front();
        if (pin != pinned.end())
            kind = kindByPath(kinds, pin->second);
        else if (totalWeight > 0)
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
        // A wall kind rolled onto a marker with no wall in reach falls back to
        // the table's first floor kind -- deterministically, with no extra
        // roll, so the seed still reproduces the floor exactly.
        if (kind != nullptr && kind->on_wall && !wallInReach(em, m.x, m.y))
        {
            for (const auto& k : kinds)
                if (!k.on_wall)
                {
                    kind = &k;
                    break;
                }
        }
        float seepX = m.x;
        float seepY = m.y;
        placeHole(em, m, kind, static_cast<int>(node.seeps.size()), seepX, seepY);
        node.seeps.push_back(
            swarm::Seep{seepX, seepY, kind != nullptr ? kind->path : std::string{}});
        node.floor.kind.push_back(kind != nullptr ? kind->path : std::string{});
    }
    node.floor.child.resize(node.seeps.size(), -1);
    node.floor.cleared.resize(node.seeps.size(), false);
    node.floor.opened.resize(node.seeps.size(), false);
    node.floor.killed.resize(node.seeps.size(), 0);
    node.floor.kind.resize(node.seeps.size());

    // The way back up, at the way in. No staging area spawns with a floor --
    // setting the kit down is HIS act, placeable when that system lands.
    {
        // Anything placed by offset must land on floor -- a fixed nudge from
        // the way in can sit inside a wall in a tight room.
        const auto ts = static_cast<float>(em.tile_map.tile_size);
        float ux = floor.spawn_x;
        float uy = floor.spawn_y;
        const float cand[3][2] = {{0.0f, -ts}, {ts, 0.0f}, {-ts, 0.0f}};
        for (const auto& c : cand)
            if (world::walkable(em, floor.spawn_x + c[0], floor.spawn_y + c[1]))
            {
                ux = std::floor((floor.spawn_x + c[0]) / ts) * ts + ts * 0.5f;
                uy = std::floor((floor.spawn_y + c[1]) / ts) * ts + ts * 0.5f;
                break;
            }
        const entt::entity up = spawn::box(em, ux, uy, 26.0f, 0.35f, 0.30f, 0.25f);
        reg.emplace<AscendSite>(up, AscendSite{16.0f}); // stand ON it to climb
        reg.get<Sprite>(up).layer = 1;
        upX = ux;
        upY = uy;
    }

    restoreSpentHoles(em, nodeId);
    beginFloor(nodeId, upX, upY);
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

// `atHole` names the hole he arrives at, or -1 for the floor's own way in.
bool enterNode(Engine& engine, EntityManager& em, int nodeId, int atHole)
{
    if (!sNodes[static_cast<std::size_t>(nodeId)].floor.area.empty())
        return enterAuthored(engine, em, nodeId, atHole);
    float sx = 0.0f;
    float sy = 0.0f;
    float ux = 0.0f;
    float uy = 0.0f;
    if (!buildNode(em, nodeId, sx, sy, ux, uy))
        return false;
    // Arriving IN FRONT of the way back up -- beside the passage, not on it, so
    // no prompt greets the landing.
    const auto ts = static_cast<float>(em.tile_map.tile_size);
    float x = ux;
    float y = uy + ts;
    if (!world::walkable(em, x, y))
    {
        x = sx;
        y = sy;
    }
    TileMapRenderer::upload(em.tile_map, em.tile_config, engine.textureManager());
    em.flow_field.last_player_col = -1;
    em.flow_field.last_player_row = -1;
    auto& reg = em.registry();
    const entt::entity p = player::entity();
    if (!standAtHole(em, nodeId, atHole))
        player::standAt(em, x, y);
    sCurrent = nodeId;
    const Node& here = sNodes[static_cast<std::size_t>(nodeId)];
    poe::log().info("descent: at depth {} (node {}, seed {})", here.floor.depth, nodeId,
                    here.floor.seed);
    return true;
}

// EVERY UNFINISHED HOLE BENEATH A WAY DOWN, nearest floor first. What he broke open and
// walked away from is still coming, however far up he goes and however many floors deep the
// trail of abandoned holes runs -- but a passage is one passage, so they arrive through it in
// turn rather than all at once.
std::vector<Owner> queueUnder(int nodeId, std::vector<Owner> found = {})
{
    if (nodeId < 0 || nodeId >= static_cast<int>(sNodes.size()))
        return found;
    const Node& node = sNodes[static_cast<std::size_t>(nodeId)];
    for (std::size_t i = 0; i < node.floor.cleared.size(); ++i)
        if (!node.floor.cleared[i] && i < node.floor.opened.size() && node.floor.opened[i])
            found.push_back(Owner{nodeId, static_cast<int>(i)});
    // Breadth before depth: the floor immediately below reaches him before what is under it.
    for (const int child : node.floor.child)
        if (child >= 0)
            found = queueUnder(child, std::move(found));
    return found;
}

// EVERY UNFINISHED HOLE ON THE FLOORS HE CAME DOWN THROUGH, nearest first. Walked along the
// way he actually came in, so what follows him down is what he personally left running -- not
// every hole in the tree that happens to sit above this depth.
std::vector<Owner> queueAbove(int nodeId)
{
    std::vector<Owner> found;
    int branch = nodeId; // the way he came down, which is the one thing NOT above him
    for (int at = nodeId; at >= 0;)
    {
        const int up = sNodes[static_cast<std::size_t>(at)].floor.from;
        if (up < 0)
            break;
        const Node& above = sNodes[static_cast<std::size_t>(up)];
        for (std::size_t i = 0; i < above.floor.cleared.size(); ++i)
            if (!above.floor.cleared[i] && i < above.floor.opened.size() && above.floor.opened[i])
                found.push_back(Owner{up, static_cast<int>(i)});
        // AND everything still running down its OTHER ways down. A floor's own holes are not
        // the whole of what he started there: what he was fighting a moment ago may have been
        // arriving through that floor from another branch entirely, and walking one step deeper
        // does not settle it. Only the branch he just came down is excluded -- that is below
        // him now rather than behind him.
        for (std::size_t i = 0; i < above.floor.child.size(); ++i)
            if (above.floor.child[i] >= 0 && above.floor.child[i] != branch)
                found = queueUnder(above.floor.child[i], std::move(found));
        branch = up;
        at = up;
    }
    return found;
}

// What a mouth draws from: below it, or above him.
std::vector<Owner> queueFor(int nodeId, const SeepSlot& mouth)
{
    if (mouth.mouth == Mouth::Up)
        return queueAbove(nodeId);
    if (mouth.mouth == Mouth::Down)
        return queueUnder(sNodes[static_cast<std::size_t>(nodeId)].floor.child.at(
            static_cast<std::size_t>(mouth.hole)));
    return {};
}

// START THE FLOOR. Its own holes first, index for index, then ONE PASSAGE PER WAY DOWN --
// each running the nearest unfinished hole beneath it, at that hole's own depth and out of
// what is left of its program. A passage is a passage: it carries the floors below in turn,
// never all at once.
void beginFloor(int nodeId, float upX, float upY)
{
    Node& node = sNodes[static_cast<std::size_t>(nodeId)];
    std::vector<swarm::Seep> seeps = node.seeps;
    for (auto& seep : seeps)
        seep.depth = node.floor.depth;
    std::vector<bool> cleared = node.floor.cleared;
    std::vector<bool> opened = node.floor.opened;
    std::vector<int> killed = node.floor.killed;

    sSlots.clear();
    for (std::size_t i = 0; i < node.seeps.size(); ++i)
        sSlots.push_back(SeepSlot{Owner{nodeId, static_cast<int>(i)}, Mouth::Own,
                                  static_cast<int>(i), node.seeps[i].x, node.seeps[i].y});

    // One passage per way down, plus the way back up: work reaches him from BOTH directions,
    // because a floor he left running does not care which way he walked out of it.
    std::vector<SeepSlot> mouths;
    for (std::size_t i = 0; i < node.floor.cleared.size(); ++i)
        if (node.floor.cleared[i])
            mouths.push_back(SeepSlot{Owner{}, Mouth::Down, static_cast<int>(i), node.seeps[i].x,
                                      node.seeps[i].y});
    if (node.floor.from >= 0)
        mouths.push_back(SeepSlot{Owner{}, Mouth::Up, -1, upX, upY});
    poe::log().info("descent: {} has {} mouth(s); came from node {}", node.floor.label,
                    mouths.size(), node.floor.from);

    for (SeepSlot mouth : mouths)
    {
        const std::vector<Owner> queue = queueFor(nodeId, mouth);
        poe::log().info("descent:   mouth {} (hole {}) -> {} waiting",
                        mouth.mouth == Mouth::Up ? "up" : "down", mouth.hole, queue.size());
        if (queue.empty())
            continue;
        const Owner& head = queue.front();
        // One hole is carried by ONE passage: two ways down can lead to the same floor now, and
        // delivered twice it would drain twice.
        if (std::any_of(sSlots.begin(), sSlots.end(), [&](const SeepSlot& taken)
                        { return taken.owner.node == head.node && taken.owner.hole == head.hole; }))
            continue;
        const Node& other = sNodes[static_cast<std::size_t>(head.node)];
        seeps.push_back(swarm::Seep{mouth.x, mouth.y,
                                    other.floor.kind.at(static_cast<std::size_t>(head.hole)),
                                    other.floor.depth});
        cleared.push_back(false);
        opened.push_back(true); // opened wherever it is; the passage only carries it
        killed.push_back(other.floor.killed.at(static_cast<std::size_t>(head.hole)));
        mouth.owner = head;
        sSlots.push_back(mouth);
        poe::log().info("descent: the way {} carries {} (depth {})",
                        mouth.mouth == Mouth::Up ? "up" : "down", poeTag(head.node, head.hole),
                        other.floor.depth);
    }
    swarm::begin("config/swarm.json", seeps, node.floor.depth, cleared, killed, opened);
    // The whole table, once, where a floor starts: what is running here and who it belongs to.
    // Anything reading wrong on the HUD is readable here first.
    const auto carried = static_cast<std::size_t>(std::count_if(
        sSlots.begin(), sSlots.end(), [](const SeepSlot& s) { return s.mouth != Mouth::Own; }));
    poe::log().info("descent: {} running {} seep(s) -- {} own, {} carried", node.floor.label,
                    sSlots.size(), sSlots.size() - carried, carried);
}

int childOf(int hole)
{
    if (sCurrent < 0 || hole < 0)
        return -1;
    const auto& child = sNodes[static_cast<std::size_t>(sCurrent)].floor.child;
    return hole < static_cast<int>(child.size()) ? child[static_cast<std::size_t>(hole)] : -1;
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

bool descend(Engine& engine, EntityManager& em, int hole)
{
    if (sCurrent < 0)
        return false;
    const auto cur = static_cast<std::size_t>(sCurrent);
    // A PASSAGE IN USE CANNOT BE TRAVELLED. What he opened down there is coming up this hole,
    // and he does not get to walk past it -- leaving a floor unfinished costs him the way back
    // into it until he has answered for what he started.
    if (!queueUnder(childOf(hole)).empty())
    {
        poe::log().info("descent: hole {} is still delivering -- finish it first", hole);
        return false;
    }
    if (hole < 0 || hole >= static_cast<int>(sNodes[cur].floor.child.size()) ||
        !sNodes[cur].floor.cleared[static_cast<std::size_t>(hole)])
    {
        poe::log().error("descent: hole {} is not descendable", hole);
        return false;
    }
    int childId = sNodes[cur].floor.child[static_cast<std::size_t>(hole)];
    const int childDepth = sNodes[cur].floor.depth + 1;
    // At an act boundary every hole above opens the SAME floor: the branches rejoin, and the
    // man who explored three of them and the man who took one arrive at the same door.
    if (childId < 0 && convergesAt(childDepth))
    {
        childId = sharedAt(childDepth);
        if (childId >= 0)
        {
            sNodes[cur].floor.child[static_cast<std::size_t>(hole)] = childId;
            poe::log().info("descent: hole {} rejoins {}", hole, floorLabel(childId));
        }
    }
    if (childId < 0)
    {
        // Indexed access on BOTH sides of the push: growing the vector moves
        // every node, and a reference held across it dangles.
        Node child;
        child.floor.depth = sNodes[cur].floor.depth + 1;
        child.floor.label = labelFor(child.floor.depth);
        child.floor.from = sCurrent;
        child.floor.from_hole = hole;
        sNodes.push_back(child);
        childId = static_cast<int>(sNodes.size()) - 1;
        sNodes[cur].floor.child[static_cast<std::size_t>(hole)] = childId;
    }
    const int fromId = sCurrent;
    // However he got here before, THIS is the way back now.
    sNodes[static_cast<std::size_t>(childId)].floor.from = sCurrent;
    sNodes[static_cast<std::size_t>(childId)].floor.from_hole = hole;
    if (enterNode(engine, em, childId, /*atHole=*/-1))
        return true;
    // A child that cannot build must not strand him in a torn-down world.
    return enterNode(engine, em, fromId, /*atHole=*/-1);
}

bool ascend(Engine& engine, EntityManager& em)
{
    if (sCurrent < 0)
        return false;
    // A PASSAGE IN USE CANNOT BE TRAVELLED, in either direction. What he left running above is
    // coming down this square, and he does not get to walk up past it any more than he gets to
    // walk down past what is coming up.
    if (!queueAbove(sCurrent).empty())
    {
        poe::log().info("descent: the way up is still delivering -- finish it first");
        return false;
    }
    const Node& node = sNodes[static_cast<std::size_t>(sCurrent)];
    if (node.floor.from < 0)
        return false; // the top floor is authored; there is nothing above it to climb to
    // The hole, not a place: where it is comes from the parent once the parent
    // is standing, which after a resume is the first time it has existed.
    return enterNode(engine, em, node.floor.from, node.floor.from_hole);
}

void refreshLeaks(EntityManager& em)
{
    for (auto [e, site] : em.registry().view<DescendSite>().each())
        site.leaking = !queueUnder(childOf(site.hole)).empty();
    // The way back up says the same thing about what is coming down it.
    const bool fromAbove = sCurrent >= 0 && !queueAbove(sCurrent).empty();
    for (auto [e, site] : em.registry().view<AscendSite>().each())
        site.leaking = fromAbove;
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

void update(Engine& engine, EntityManager& em, float dt)
{
    (void)engine;
    syncArea(em);
    refreshLeaks(em);
    // WHAT EACH SEEP HAS LOST GOES TO THE FLOOR THAT OWNS IT -- a passage's kills belong to
    // the floor below, not to the one he is standing on. Taken every frame rather than at some
    // exit, because quitting is an exit nobody gets to run code on.
    const std::vector<int>& lost = swarm::progress();
    for (std::size_t i = 0; i < sSlots.size() && i < lost.size(); ++i)
    {
        const Owner& owner = sSlots[i].owner;
        if (owner.node < 0 || owner.node >= static_cast<int>(sNodes.size()))
            continue;
        auto& killed = sNodes[static_cast<std::size_t>(owner.node)].floor.killed;
        if (static_cast<std::size_t>(owner.hole) < killed.size())
            killed[static_cast<std::size_t>(owner.hole)] = lost[i];
    }
    if (sCurrent < 0)
        return;
    Node& node = sNodes[static_cast<std::size_t>(sCurrent)];
    for (std::size_t s = 0; s < sSlots.size(); ++s)
    {
        const SeepSlot& slot = sSlots[s];
        const Owner owner = slot.owner;
        if (owner.node < 0)
            continue;
        auto& ownerCleared = sNodes[static_cast<std::size_t>(owner.node)].floor.cleared;
        const auto oh = static_cast<std::size_t>(owner.hole);
        if (oh >= ownerCleared.size() || ownerCleared[oh] ||
            !swarm::seepCleared(em, static_cast<int>(s)))
            continue;
        ownerCleared[oh] = true;
        if (slot.mouth != Mouth::Own)
        {
            // A passage finished what it was carrying; the next thing below takes its place.
            poe::log().info("descent: node {} hole {} spent from above", owner.node, owner.hole);
            continue;
        }
        const std::size_t i = oh;
        // The hole is spent: from spawner to way down. The hole's own art carries the
        // component -- a second sprite would stack and fight.
        for (const auto [e, art] : em.registry().view<SeepArt>().each())
            if (art.hole == static_cast<int>(i))
            {
                DescendSite site;
                site.radius = siteFeel().reach;
                site.hole = art.hole;
                site.spawn_x = node.seeps[i].x;
                site.spawn_y = node.seeps[i].y;
                em.registry().emplace<DescendSite>(e, site);
            }
        poe::log().info("descent: hole {} spent -- a way down now", i);
    }

    // The floor's own smell, from exactly what is producing: an unspent hole he opened, or a
    // way down with something coming up it.
    sReekTimer += dt;
    if (sReekTimer >= kReekEvery)
    {
        sReekTimer = 0.0f;
        for (std::size_t i = 0; i < node.seeps.size(); ++i)
        {
            const bool pressing = i < node.floor.opened.size() && node.floor.opened[i] &&
                                  i < node.floor.cleared.size() && !node.floor.cleared[i];
            const bool carrying = i < node.floor.cleared.size() && node.floor.cleared[i] &&
                                  !queueUnder(node.floor.child[i]).empty();
            if (pressing || carrying)
                reek(em, node.seeps[i].x, node.seeps[i].y, /*rising=*/true);
        }
        // And the way back up, when something he left above is coming down it.
        for (const SeepSlot& slot : sSlots)
            if (slot.mouth == Mouth::Up && slot.owner.node >= 0)
                reek(em, slot.x, slot.y, /*rising=*/false);
    }

    // Each passage carries the nearest thing still running on the other side of it. When that
    // finishes -- or when he opens something new over there -- the passage picks up whatever is
    // next, without disturbing the floor he is standing on.
    for (std::size_t s = 0; s < sSlots.size(); ++s)
    {
        SeepSlot& slot = sSlots[s];
        if (slot.mouth == Mouth::Own)
            continue;
        const std::vector<Owner> queue = queueFor(sCurrent, slot);
        if (queue.empty())
        {
            swarm::retarget(static_cast<int>(s), swarm::Seep{}, 0);
            slot.owner = Owner{};
            continue;
        }
        const Owner& head = queue.front();
        if (head.node == slot.owner.node && head.hole == slot.owner.hole)
            continue;
        const Node& other = sNodes[static_cast<std::size_t>(head.node)];
        swarm::retarget(static_cast<int>(s),
                        swarm::Seep{slot.x, slot.y,
                                    other.floor.kind.at(static_cast<std::size_t>(head.hole)),
                                    other.floor.depth},
                        other.floor.killed.at(static_cast<std::size_t>(head.hole)));
        slot.owner = head;
    }
}

int openableUnderfoot(const EntityManager& em, float x, float y)
{
    if (sCurrent < 0)
        return -1;
    const Node& node = sNodes[static_cast<std::size_t>(sCurrent)];
    const float reach = siteFeel().reach;
    for (std::size_t i = 0; i < node.seeps.size(); ++i)
    {
        if (i < node.floor.opened.size() && node.floor.opened[i])
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
    if (i >= node.floor.opened.size() || node.floor.opened[i])
        return false;
    node.floor.opened[i] = true;
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
    for (std::size_t i = 0; i < node.floor.cleared.size(); ++i)
        if (!node.floor.cleared[i] && i < node.floor.opened.size() && node.floor.opened[i])
            ++fronts;
    // A passage is a front too: something is coming up it, and he is standing in front of it.
    for (std::size_t i = 0; i < node.floor.cleared.size(); ++i)
        if (node.floor.cleared[i] && !queueUnder(node.floor.child[i]).empty())
            ++fronts;
    return fronts;
}

std::vector<Point> exclusions()
{
    std::vector<Point> out;
    if (sCurrent < 0)
        return out;
    const Node& node = sNodes[static_cast<std::size_t>(sCurrent)];

    // What each seep is doing, indexed by the mouth it arrives at, so a hole that is carrying
    // reads as working even though its own program is long spent.
    const auto workingAt = [&](int hole, int& wave, int& waves)
    {
        for (std::size_t s = 0; s < sSlots.size(); ++s)
        {
            const SeepSlot& slot = sSlots[s];
            const bool mine = slot.mouth == Mouth::Up ? hole < 0 : slot.hole == hole;
            if (!mine || slot.owner.node < 0 || swarm::seepSealed(static_cast<int>(s)))
                continue;
            const auto& cleared = sNodes[static_cast<std::size_t>(slot.owner.node)].floor.cleared;
            const auto oh = static_cast<std::size_t>(slot.owner.hole);
            if (oh >= cleared.size() || cleared[oh])
                continue;
            wave = swarm::seepWave(static_cast<int>(s));
            waves = swarm::seepWaves(static_cast<int>(s));
            return true;
        }
        return false;
    };

    // The way he came in first, when anything is coming down it: it is the nearest thing to him
    // and the least expected.
    int wave = 0;
    int waves = 0;
    if (workingAt(-1, wave, waves))
        out.push_back(Point{poeTag(sCurrent, -1), PointState::Working, wave, waves});

    for (std::size_t i = 0; i < node.seeps.size(); ++i)
    {
        Point point;
        point.tag = poeTag(sCurrent, static_cast<int>(i));
        if (workingAt(static_cast<int>(i), point.wave, point.waves))
            point.state = PointState::Working;
        else if (i < node.floor.cleared.size() && node.floor.cleared[i])
            point.state = PointState::Cleared;
        else
            point.state = PointState::Sealed;
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

std::string beyondLabel(int hole, bool downward)
{
    if (sCurrent < 0)
        return {};
    const Node& node = sNodes[static_cast<std::size_t>(sCurrent)];
    if (downward)
    {
        // A place he has been has a name. One he has not is a QUESTION, and saying so is the
        // point: walking back into a floor he cleared is walking, and opening one he has never
        // seen is a commitment. Naming it in advance would flatten the difference.
        const int child = childOf(hole);
        if (child >= 0)
            return floorLabel(child);
        // Except where the branches rejoin: at an act boundary every hole above opens the SAME
        // floor, so one he has not dug yet still leads somewhere he has been. The link is only
        // made when he digs, but the destination is known before he does, and calling a place
        // he has walked through a question would be a lie.
        if (convergesAt(node.floor.depth + 1))
            if (const int shared = sharedAt(node.floor.depth + 1); shared >= 0)
                return floorLabel(shared);
        return std::string{"?"};
    }
    return floorLabel(node.floor.from);
}

int stepDir(int hole, bool downward)
{
    if (sCurrent < 0)
        return 0;
    const Node& node = sNodes[static_cast<std::size_t>(sCurrent)];
    // Worked out from the DEPTHS rather than from which act was asked for, so a wall hole that
    // opens a room at the same depth marks itself as across without anything here changing.
    int there = node.floor.depth;
    if (downward)
    {
        const int child = childOf(hole);
        there =
            child >= 0 ? sNodes[static_cast<std::size_t>(child)].floor.depth : node.floor.depth + 1;
    }
    else if (node.floor.from >= 0)
        there = sNodes[static_cast<std::size_t>(node.floor.from)].floor.depth;
    return there > node.floor.depth ? 1 : there < node.floor.depth ? -1 : 0;
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
