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

// Which floor's hole a seep is running. A floor's own holes own themselves; a passage owns
// whatever it is currently carrying from below.
struct Owner
{
    int node = -1;
    int hole = -1;
};

std::vector<Node> sNodes;
// Parallel to the swarm's seeps: locals first, then one entry per way down on this floor.
std::vector<Owner> sSeepOwner;
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
        if (art.hole < 0 || i >= node.floor.opened.size() || !node.floor.opened[i])
            continue;
        if (auto* spr = reg.try_get<Sprite>(e))
            spr->src_x = art.open_x;
    }
    for (const auto [e, art] : reg.view<SeepArt>().each())
    {
        const auto i = static_cast<std::size_t>(art.hole);
        if (art.hole < 0 || i >= node.floor.cleared.size() || !node.floor.cleared[i] ||
            reg.all_of<DigSite>(e))
            continue;
        DigSite dig;
        dig.radius = siteFeel().reach;
        dig.hole = art.hole;
        dig.spawn_x = node.seeps[i].x;
        dig.spawn_y = node.seeps[i].y;
        reg.emplace<DigSite>(e, dig);
    }
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
    beginFloor(id);
    poe::log().info("descent: '{}' is a floor -- {} hole(s) at depth {}", area, node.seeps.size(),
                    node.floor.depth);
    return id;
}

// The floor he is standing in follows the world. Generated space is whatever
// dig or ascend put him in; an authored level is a floor when it has holes and
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

// START THE FLOOR. Its own holes first, index for index, then ONE PASSAGE PER WAY DOWN --
// each running the nearest unfinished hole beneath it, at that hole's own depth and out of
// what is left of its program. A passage is a passage: it carries the floors below in turn,
// never all at once.
void beginFloor(int nodeId)
{
    Node& node = sNodes[static_cast<std::size_t>(nodeId)];
    std::vector<swarm::Seep> seeps = node.seeps;
    for (auto& seep : seeps)
        seep.depth = node.floor.depth;
    std::vector<bool> cleared = node.floor.cleared;
    std::vector<bool> opened = node.floor.opened;
    std::vector<int> killed = node.floor.killed;

    sSeepOwner.clear();
    for (std::size_t i = 0; i < node.seeps.size(); ++i)
        sSeepOwner.push_back(Owner{nodeId, static_cast<int>(i)});

    for (std::size_t i = 0; i < node.floor.cleared.size(); ++i)
    {
        if (!node.floor.cleared[i])
            continue; // not a way down yet, so nothing can come up it
        const std::vector<Owner> queue = queueUnder(node.floor.child[i]);
        if (queue.empty())
            continue;
        const Owner& head = queue.front();
        const Node& below = sNodes[static_cast<std::size_t>(head.node)];
        seeps.push_back(swarm::Seep{node.seeps[i].x, node.seeps[i].y,
                                    below.floor.kind.at(static_cast<std::size_t>(head.hole)),
                                    below.floor.depth});
        cleared.push_back(false);
        opened.push_back(true); // it was opened down there; the passage only carries it
        killed.push_back(below.floor.killed.at(static_cast<std::size_t>(head.hole)));
        sSeepOwner.push_back(head);
        poe::log().info("descent: a way down carries node {} hole {} (depth {})", head.node,
                        head.hole, below.floor.depth);
    }
    swarm::begin("config/swarm.json", seeps, node.floor.depth, cleared, killed, opened);
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
        const nlohmann::json site = j.value("dig_site", nlohmann::json::object());
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

bool dig(Engine& engine, EntityManager& em, int hole)
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
        poe::log().error("descent: hole {} is not diggable", hole);
        return false;
    }
    int childId = sNodes[cur].floor.child[static_cast<std::size_t>(hole)];
    if (childId < 0)
    {
        // Indexed access on BOTH sides of the push: growing the vector moves
        // every node, and a reference held across it dangles.
        Node child;
        child.floor.depth = sNodes[cur].floor.depth + 1;
        child.floor.parent = sCurrent;
        child.floor.parent_hole = hole;
        sNodes.push_back(child);
        childId = static_cast<int>(sNodes.size()) - 1;
        sNodes[cur].floor.child[static_cast<std::size_t>(hole)] = childId;
    }
    const int fromId = sCurrent;
    if (enterNode(engine, em, childId, /*atHole=*/-1))
        return true;
    // A child that cannot build must not strand him in a torn-down world.
    return enterNode(engine, em, fromId, /*atHole=*/-1);
}

bool ascend(Engine& engine, EntityManager& em)
{
    if (sCurrent < 0)
        return false;
    const Node& node = sNodes[static_cast<std::size_t>(sCurrent)];
    if (node.floor.parent < 0)
        return false; // the top floor is authored; there is nothing above it to climb to
    // The hole, not a place: where it is comes from the parent once the parent
    // is standing, which after a resume is the first time it has existed.
    return enterNode(engine, em, node.floor.parent, node.floor.parent_hole);
}

void refreshLeaks(EntityManager& em)
{
    for (auto [e, site] : em.registry().view<DigSite>().each())
        site.leaking = !queueUnder(childOf(site.hole)).empty();
}

// One puff above a hole that is doing something: a handful of blobs at different sizes and
// offsets, drifting at slightly different rates. Square particles read as squares when there
// is one of them and as a cloud when there are several overlapping and none of them agree --
// so the variation IS the effect, not decoration on it.
void reek(EntityManager& em, float x, float y)
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
        reg.emplace<Velocity>(blob, Velocity{lean + wander(rng), -kReekRise + wander(rng)});
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
    for (std::size_t i = 0; i < sSeepOwner.size() && i < lost.size(); ++i)
    {
        const Owner& owner = sSeepOwner[i];
        if (owner.node < 0 || owner.node >= static_cast<int>(sNodes.size()))
            continue;
        auto& killed = sNodes[static_cast<std::size_t>(owner.node)].floor.killed;
        if (static_cast<std::size_t>(owner.hole) < killed.size())
            killed[static_cast<std::size_t>(owner.hole)] = lost[i];
    }
    if (sCurrent < 0)
        return;
    Node& node = sNodes[static_cast<std::size_t>(sCurrent)];
    for (std::size_t s = 0; s < sSeepOwner.size(); ++s)
    {
        const Owner owner = sSeepOwner[s];
        if (owner.node < 0)
            continue;
        auto& ownerCleared = sNodes[static_cast<std::size_t>(owner.node)].floor.cleared;
        const auto oh = static_cast<std::size_t>(owner.hole);
        if (oh >= ownerCleared.size() || ownerCleared[oh] ||
            !swarm::seepCleared(em, static_cast<int>(s)))
            continue;
        ownerCleared[oh] = true;
        if (owner.node != sCurrent)
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
                DigSite dig;
                dig.radius = siteFeel().reach;
                dig.hole = art.hole;
                dig.spawn_x = node.seeps[i].x;
                dig.spawn_y = node.seeps[i].y;
                em.registry().emplace<DigSite>(e, dig);
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
                reek(em, node.seeps[i].x, node.seeps[i].y);
        }
    }

    // Each passage carries the nearest thing still running below it. When that finishes -- or
    // when he opens something new down there -- the passage picks up whatever is next without
    // disturbing the floor he is standing on.
    for (std::size_t s = node.seeps.size(); s < sSeepOwner.size(); ++s)
    {
        const int hole = static_cast<int>(s - node.seeps.size());
        int passage = -1;
        int seen = 0;
        for (std::size_t i = 0; i < node.floor.cleared.size(); ++i)
            if (node.floor.cleared[i] && !queueUnder(node.floor.child[i]).empty() && seen++ == hole)
            {
                passage = static_cast<int>(i);
                break;
            }
        const std::vector<Owner> queue =
            passage >= 0 ? queueUnder(node.floor.child[static_cast<std::size_t>(passage)])
                         : std::vector<Owner>{};
        if (queue.empty())
        {
            swarm::retarget(static_cast<int>(s), swarm::Seep{}, 0);
            sSeepOwner[s] = Owner{};
            continue;
        }
        const Owner& head = queue.front();
        if (head.node == sSeepOwner[s].node && head.hole == sSeepOwner[s].hole)
            continue;
        const Node& below = sNodes[static_cast<std::size_t>(head.node)];
        swarm::retarget(static_cast<int>(s),
                        swarm::Seep{node.seeps[static_cast<std::size_t>(passage)].x,
                                    node.seeps[static_cast<std::size_t>(passage)].y,
                                    below.floor.kind.at(static_cast<std::size_t>(head.hole)),
                                    below.floor.depth},
                        below.floor.killed.at(static_cast<std::size_t>(head.hole)));
        sSeepOwner[s] = head;
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

bool floorHasWork()
{
    return workUnderway(sCurrent);
}

} // namespace descent
