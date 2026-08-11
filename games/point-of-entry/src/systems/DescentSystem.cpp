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
struct Node
{
    unsigned seed = 0; // 0 until first generation picks one
    int depth = 0;
    int parent = -1;
    int parent_hole = -1;
    std::vector<int> child;         // per hole: node index, -1 = never dug
    std::vector<bool> cleared;      // per hole: assault spent?
    std::vector<swarm::Seep> seeps; // rebuilt every generation, same every time
    std::vector<std::string> leak;  // per hole: the vein's preview creature
};

std::vector<Node> sNodes;
int sCurrent = -1;         // node the player stands in; -1 = not in the dig
std::string sSurfaceLevel; // the authored level the root hangs under

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
void placeHole(EntityManager& em, const floorgen::Marker& m, const SeepKind* kind, int index,
               float& seepX, float& seepY)
{
    const entt::entity hole = spawn::box(em, m.x, m.y, kPoeSize, 0.75f, 0.15f, 0.15f);
    em.registry().emplace<SeepArt>(hole, SeepArt{index});
    seepX = m.x;
    seepY = m.y;
    if (kind == nullptr || !kind->def.ok)
        return;
    auto& spr = em.registry().get<Sprite>(hole);
    spr.texture_path = kind->def.sheet;
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
        floorgen::generate(em, "config/floor.json", "config/rooms", node.seed);
    if (!floor.ok)
    {
        poe::log().error("descent: could not build floor at depth {}", node.depth);
        return false;
    }
    node.seed = floor.seed; // first generation picks; every later visit repeats
    spawnX = floor.spawn_x;
    spawnY = floor.spawn_y;

    std::vector<SeepKind> kinds;
    std::unordered_map<char, std::string> pinned;
    loadKindTable(kinds, pinned);
    int totalWeight = 0;
    for (const auto& k : kinds)
        totalWeight += k.weight;

    node.seeps.clear();
    node.leak.clear();
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
        node.leak.push_back(kind != nullptr ? kind->first_creature : std::string{});
    }
    node.child.resize(node.seeps.size(), -1);
    node.cleared.resize(node.seeps.size(), false);

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

    // Already-cleared holes come back as dig sites, not spawners: the hole's
    // own art carries the component -- a second sprite would stack and fight.
    for (const auto [e, art] : reg.view<SeepArt>().each())
        if (art.hole >= 0 && art.hole < static_cast<int>(node.cleared.size()) &&
            node.cleared[static_cast<std::size_t>(art.hole)])
        {
            DigSite dig;
            dig.depth = node.depth + 1;
            dig.hole = art.hole;
            dig.trickle = node.leak[static_cast<std::size_t>(art.hole)];
            dig.open = node.child[static_cast<std::size_t>(art.hole)] >= 0;
            dig.spawn_x = node.seeps[static_cast<std::size_t>(art.hole)].x;
            dig.spawn_y = node.seeps[static_cast<std::size_t>(art.hole)].y;
            reg.emplace<DigSite>(e, dig);
        }

    swarm::begin("config/swarm.json", node.seeps, node.depth, node.cleared);
    return true;
}

// Enter a node: build, upload, and stand the player at (x,y) -- or at the
// floor's own way in when the caller passes atWayIn.
bool enterNode(Engine& engine, EntityManager& em, int nodeId, bool atWayIn, float x, float y)
{
    float sx = 0.0f;
    float sy = 0.0f;
    float ux = 0.0f;
    float uy = 0.0f;
    if (!buildNode(em, nodeId, sx, sy, ux, uy))
        return false;
    if (atWayIn)
    {
        // Descending arrives IN FRONT of the way back up -- beside the
        // passage, not on it, so no prompt greets the landing.
        const auto ts = static_cast<float>(em.tile_map.tile_size);
        x = ux;
        y = uy + ts;
        if (!world::walkable(em, x, y))
        {
            x = sx;
            y = sy;
        }
    }
    TileMapRenderer::upload(em.tile_map, em.tile_config, engine.textureManager());
    em.flow_field.last_player_col = -1;
    em.flow_field.last_player_row = -1;
    auto& reg = em.registry();
    const entt::entity p = player::entity();
    if (reg.valid(p))
    {
        auto& t = reg.get<Transform>(p);
        t.x = x;
        t.y = y;
        reg.get<PreviousTransform>(p) = PreviousTransform{x, y};
        if (auto* cam = reg.try_get<Camera>(p))
        {
            cam->x = x;
            cam->y = y;
            cam->prev_x = x;
            cam->prev_y = y;
        }
    }
    sCurrent = nodeId;
    poe::log().info("descent: at depth {} (node {}, seed {})", currentDepth(), nodeId,
                    sNodes[static_cast<std::size_t>(nodeId)].seed);
    return true;
}

} // namespace

void reset()
{
    sNodes.clear();
    sCurrent = -1;
    sSurfaceLevel.clear();
}

void leave()
{
    sCurrent = -1;
}

bool enterRoot(Engine& engine, EntityManager& em)
{
    sSurfaceLevel = travel::currentArea();
    if (sNodes.empty())
    {
        Node root;
        root.depth = 0;
        sNodes.push_back(root);
    }
    return enterNode(engine, em, 0, /*atWayIn=*/true, 0.0f, 0.0f);
}

bool dig(Engine& engine, EntityManager& em, int hole)
{
    if (sCurrent < 0)
        return false;
    const auto cur = static_cast<std::size_t>(sCurrent);
    if (hole < 0 || hole >= static_cast<int>(sNodes[cur].child.size()) ||
        !sNodes[cur].cleared[static_cast<std::size_t>(hole)])
    {
        poe::log().error("descent: hole {} is not diggable", hole);
        return false;
    }
    int childId = sNodes[cur].child[static_cast<std::size_t>(hole)];
    if (childId < 0)
    {
        // Indexed access on BOTH sides of the push: growing the vector moves
        // every node, and a reference held across it dangles.
        Node child;
        child.depth = sNodes[cur].depth + 1;
        child.parent = sCurrent;
        child.parent_hole = hole;
        sNodes.push_back(child);
        childId = static_cast<int>(sNodes.size()) - 1;
        sNodes[cur].child[static_cast<std::size_t>(hole)] = childId;
    }
    const int fromId = sCurrent;
    if (enterNode(engine, em, childId, /*atWayIn=*/true, 0.0f, 0.0f))
        return true;
    // A child that cannot build must not strand him in a torn-down world.
    return enterNode(engine, em, fromId, /*atWayIn=*/true, 0.0f, 0.0f);
}

bool ascend(Engine& engine, EntityManager& em)
{
    if (sCurrent < 0)
        return false;
    const Node& node = sNodes[static_cast<std::size_t>(sCurrent)];
    if (node.parent < 0)
    {
        // The root climbs out into the authored basement -- standing at its
        // dig site, the other end of the same passage.
        sCurrent = -1;
        if (!sSurfaceLevel.empty() && travel::enter(engine, em, sSurfaceLevel))
        {
            auto& reg = em.registry();
            for (const auto [e, site] : reg.view<DigSite>().each())
            {
                const entt::entity p = player::entity();
                if (!reg.valid(p))
                    break;
                auto& t = reg.get<Transform>(p);
                t.x = site.spawn_x;
                t.y = site.spawn_y + 24.0f;
                reg.get<PreviousTransform>(p) = PreviousTransform{t.x, t.y};
                if (auto* cam = reg.try_get<Camera>(p))
                {
                    cam->x = t.x;
                    cam->y = t.y;
                    cam->prev_x = t.x;
                    cam->prev_y = t.y;
                }
                break;
            }
            return true;
        }
        poe::log().error("descent: no surface to climb out to");
        return false;
    }
    const Node& parent = sNodes[static_cast<std::size_t>(node.parent)];
    const swarm::Seep at = parent.seeps[static_cast<std::size_t>(node.parent_hole)];
    return enterNode(engine, em, node.parent, /*atWayIn=*/false, at.x, at.y + 24.0f);
}

void update(Engine& engine, EntityManager& em)
{
    (void)engine;
    if (sCurrent < 0)
        return;
    Node& node = sNodes[static_cast<std::size_t>(sCurrent)];
    for (std::size_t i = 0; i < node.cleared.size(); ++i)
    {
        if (node.cleared[i] || !swarm::seepCleared(em, static_cast<int>(i)))
            continue;
        // The hole is spent: from spawner to way down, leaking its preview.
        // The hole's own art carries the component -- no second sprite.
        node.cleared[i] = true;
        for (const auto [e, art] : em.registry().view<SeepArt>().each())
            if (art.hole == static_cast<int>(i))
            {
                DigSite dig;
                dig.depth = node.depth + 1;
                dig.hole = art.hole;
                dig.trickle = node.leak[i];
                dig.open = node.child[i] >= 0; // quiet until first descended
                dig.spawn_x = node.seeps[i].x;
                dig.spawn_y = node.seeps[i].y;
                em.registry().emplace<DigSite>(e, dig);
            }
        poe::log().info("descent: hole {} spent -- diggable, leaking '{}'", i, node.leak[i]);
    }
}

bool active()
{
    return sCurrent >= 0;
}

int currentDepth()
{
    return sCurrent >= 0 ? sNodes[static_cast<std::size_t>(sCurrent)].depth : 0;
}

bool rootOpened()
{
    return !sNodes.empty();
}

} // namespace descent
