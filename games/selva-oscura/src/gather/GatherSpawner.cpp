#include "gather/GatherSpawner.h"

#include "AppState.h"
#include "AppStateGlobal.h"
#include "Formulas.h"
#include "WallClock.h"
#include "ecs/Items.h"
#include "gameplay/Actor.h"
#include "items/ItemRegistry.h"
#include "loot/Pickups.h"
#include "ops/LootOps.h"
#include "world/Collision.h"
#include "world/Terrain.h"
#include "world/Territory.h"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace selva::gather
{

namespace
{

constexpr const char* kSelvaTerritoryId = "surface";
constexpr int kMaxPlacementAttempts = 16;

// Parsed shape of one gather flow config (config/gather_nodes/<id>.json).
// ONE flow per zone (Wood = wood_forage.json today). The drop_pool
// rolls per-spawn to pick which material instantiates -- the system
// is universal across materials, not per-material.
struct NodeConfig
{
    std::string config_path; // path the JSON was loaded from
    std::string id;
    std::string prompt_label;
    int active_cap = 0;
    std::string respawn_seconds_key;
    glm::vec2 sample_origin_xz{0.0f, 0.0f};
    float sample_radius = 0.0f;
    float min_distance_to_props = 0.0f;
    float min_distance_to_nodes = 0.0f;
    float min_distance_to_player = 0.0f;
    // Cumulative-probability table for quality rolling. Each entry's
    // weight is cumulative from 0..1; roll a uniform u and pick the
    // first entry with cumulative >= u. SHARED across all materials in
    // the pool -- quality is a per-instance roll independent of which
    // material was chosen.
    struct QualityWeight
    {
        engine::ecs::QualityTier tier;
        float cumulative;
    };
    std::vector<QualityWeight> quality_table;
    // Weighted-selection pool of materials this flow can spawn. Each
    // entry's weight is an integer; engine::ops::loot::rollWeightedPool
    // normalizes by sum. Adding a fourth material is a single new
    // entry; no other field needs retuning.
    engine::ecs::WeightedPool drop_pool;
};

std::vector<NodeConfig>& configs()
{
    static std::vector<NodeConfig> cs;
    return cs;
}

// Per-active-node tracking of the registered Interactable id (so we
// can unregister on grant / cycle reset). Keyed by NodeState.id.
std::unordered_map<std::uint32_t, selva::interact::Id>& interactByNodeId()
{
    static std::unordered_map<std::uint32_t, selva::interact::Id> m;
    return m;
}

std::mt19937& rng()
{
    static std::mt19937 r{std::random_device{}()};
    return r;
}

float frand(float lo, float hi)
{
    std::uniform_real_distribution<float> d(lo, hi);
    return d(rng());
}

engine::ecs::QualityTier parseQualityTier(const std::string& s)
{
    if (s == "crude")
        return engine::ecs::QualityTier::Crude;
    if (s == "common")
        return engine::ecs::QualityTier::Common;
    if (s == "fine")
        return engine::ecs::QualityTier::Fine;
    if (s == "superior")
        return engine::ecs::QualityTier::Superior;
    if (s == "masterwork")
        return engine::ecs::QualityTier::Masterwork;
    return engine::ecs::QualityTier::Common;
}

bool loadOneConfig(const std::filesystem::path& path, NodeConfig& out)
{
    std::ifstream f(path);
    if (!f.is_open())
        return false;
    nlohmann::json j;
    try
    {
        f >> j;
    }
    catch (const std::exception&)
    {
        return false;
    }

    out.config_path = path.generic_string();
    out.id = j.value("id", std::string{});
    out.prompt_label = j.value("prompt_label", std::string{});
    out.active_cap = j.value("active_cap", 0);
    out.respawn_seconds_key = j.value("respawn_seconds_key", std::string{});
    out.min_distance_to_props = j.value("min_distance_to_props", 0.0f);
    out.min_distance_to_nodes = j.value("min_distance_to_nodes", 0.0f);
    out.min_distance_to_player = j.value("min_distance_to_player", 0.0f);

    if (j.contains("sample_origin_xz") && j["sample_origin_xz"].is_array() &&
        j["sample_origin_xz"].size() == 2)
    {
        out.sample_origin_xz.x = j["sample_origin_xz"][0].get<float>();
        out.sample_origin_xz.y = j["sample_origin_xz"][1].get<float>();
    }
    out.sample_radius = j.value("sample_radius", 0.0f);

    if (j.contains("quality_lottery") && j["quality_lottery"].is_object())
    {
        float cum = 0.0f;
        for (auto it = j["quality_lottery"].begin(); it != j["quality_lottery"].end(); ++it)
        {
            if (std::string(it.key()).rfind("_comment", 0) == 0)
                continue;
            const float w = it.value().get<float>();
            cum += w;
            out.quality_table.push_back({parseQualityTier(it.key()), cum});
        }
        if (cum > 0.0f)
            for (auto& e : out.quality_table)
                e.cumulative /= cum;
    }

    if (j.contains("drop_pool") && j["drop_pool"].is_array())
    {
        for (const auto& entry : j["drop_pool"])
        {
            if (!entry.is_object() || !entry.contains("item"))
                continue;
            engine::ecs::WeightedEntry we;
            we.config_path = entry["item"].get<std::string>();
            we.weight = entry.value("weight", 1);
            out.drop_pool.entries.push_back(std::move(we));
        }
    }

    if (out.id.empty() || out.drop_pool.entries.empty() || out.active_cap <= 0 ||
        out.quality_table.empty())
    {
        std::fprintf(stderr, "[gather] config %s missing required fields\n", path.string().c_str());
        return false;
    }
    return true;
}

engine::ecs::QualityTier rollQuality(const NodeConfig& cfg)
{
    const float u = frand(0.0f, 1.0f);
    for (const auto& e : cfg.quality_table)
        if (u <= e.cumulative)
            return e.tier;
    return cfg.quality_table.back().tier;
}

// Walkability: territory check + slope check + prop-distance check.
// Per-node-type spacing + player bias are handled by the caller (they
// need access to other live nodes / the player pos, which the
// walkability predicate doesn't).
bool isWalkable(const NodeConfig& cfg, float x, float z)
{
    const float ground_y = selva::world::groundHeight(x, z);
    const glm::vec3 candidate(x, ground_y + 0.5f, z);
    if (engine::world::regionIdAtPosition(candidate) != kSelvaTerritoryId)
        return false;

    constexpr float slope_radius = 0.5f;
    constexpr float slope_threshold = 0.4f;
    const float h0 = ground_y;
    const float h_px = selva::world::groundHeight(x + slope_radius, z);
    const float h_nx = selva::world::groundHeight(x - slope_radius, z);
    const float h_pz = selva::world::groundHeight(x, z + slope_radius);
    const float h_nz = selva::world::groundHeight(x, z - slope_radius);
    const float dh = std::max(
        {std::abs(h_px - h0), std::abs(h_nx - h0), std::abs(h_pz - h0), std::abs(h_nz - h0)});
    if (dh > slope_threshold)
        return false;

    // Prop-distance check: trees and rocks are CylinderColliders in
    // the active CollisionRegion. Reject candidate XZ within
    // min_distance_to_props of any cylinder (keeps drops from clipping
    // trunks).
    const auto& region = selva::world::currentRegion();
    const float min_d2 = cfg.min_distance_to_props * cfg.min_distance_to_props;
    for (const auto& cyl : region.cylinders)
    {
        const float dx = x - cyl.center.x;
        const float dz = z - cyl.center.z;
        if (dx * dx + dz * dz < min_d2)
            return false;
    }
    return true;
}

bool tooCloseToOtherNodes(const NodeConfig& cfg, const selva::PlayerProfile& p, float x, float z)
{
    const float min_d2 = cfg.min_distance_to_nodes * cfg.min_distance_to_nodes;
    for (const auto& n : p.active_gather_nodes)
    {
        if (n.node_config_path != cfg.config_path)
            continue;
        const float dx = x - n.pos_x;
        const float dz = z - n.pos_z;
        if (dx * dx + dz * dz < min_d2)
            return true;
    }
    return false;
}

bool tooCloseToPlayer(const NodeConfig& cfg, float x, float z)
{
    // Only enforced when the player is currently in the Wood; if the
    // player is descending in Hell, "away from player" is moot.
    const auto& pa = selva::gameplay::player();
    const glm::vec3 pp(pa.pos.x, pa.pos.y, pa.pos.z);
    if (engine::world::regionIdAtPosition(pp) != kSelvaTerritoryId)
        return false;
    const float dx = x - pa.pos.x;
    const float dz = z - pa.pos.z;
    const float min_d2 = cfg.min_distance_to_player * cfg.min_distance_to_player;
    return dx * dx + dz * dz < min_d2;
}

int liveCountForConfig(const selva::PlayerProfile& p, const NodeConfig& cfg)
{
    int n = 0;
    for (const auto& s : p.active_gather_nodes)
        if (s.node_config_path == cfg.config_path)
            ++n;
    return n;
}

selva::gather::FlowState& findOrCreateFlow(selva::PlayerProfile& p, const NodeConfig& cfg)
{
    for (auto& f : p.gather_flows)
        if (f.node_config_path == cfg.config_path)
            return f;
    selva::gather::FlowState fs;
    fs.node_config_path = cfg.config_path;
    fs.last_spawn_wallclock = 0.0f;
    p.gather_flows.push_back(fs);
    return p.gather_flows.back();
}

float resolveRespawnSeconds(const NodeConfig& /*cfg*/)
{
    // All flows today share the wood_gather_respawn_seconds tunable.
    // When per-flow respawn rates are needed (a different zone wanting
    // a slower cadence), switch on cfg.respawn_seconds_key and route
    // each key to its formulas.json field. Stays a single source of
    // truth (formulas.json) either way.
    return selva::formulas::current().gather.wood_gather_respawn_seconds;
}

// Rejection-sample one valid XZ inside the config's sampling disc.
// Returns false if no candidate passes within kMaxPlacementAttempts.
bool sampleSpawnXZ(const NodeConfig& cfg, const selva::PlayerProfile& p, float& out_x, float& out_z)
{
    for (int i = 0; i < kMaxPlacementAttempts; ++i)
    {
        // Uniform disc sampling: r = R*sqrt(u1), theta = 2*pi*u2.
        const float u1 = frand(0.0f, 1.0f);
        const float u2 = frand(0.0f, 1.0f);
        const float r = cfg.sample_radius * std::sqrt(u1);
        const float theta = u2 * 6.283185307179586f;
        const float x = cfg.sample_origin_xz.x + r * std::cos(theta);
        const float z = cfg.sample_origin_xz.y + r * std::sin(theta);
        if (!isWalkable(cfg, x, z))
            continue;
        if (tooCloseToOtherNodes(cfg, p, x, z))
            continue;
        if (tooCloseToPlayer(cfg, x, z))
            continue;
        out_x = x;
        out_z = z;
        return true;
    }
    return false;
}

// Spawn one node: roll material from the flow's weighted drop_pool,
// roll quality, allocate id, persist NodeState. The caller's next
// syncInteractables pass will register the loot pickup. Returns false
// if placement sampling fails or the pool is empty.
bool spawnOneNode(selva::PlayerProfile& p, const NodeConfig& cfg)
{
    const std::string picked_material = engine::ops::loot::rollWeightedPool(cfg.drop_pool, rng());
    if (picked_material.empty())
        return false;

    float x = 0.0f;
    float z = 0.0f;
    if (!sampleSpawnXZ(cfg, p, x, z))
        return false;

    selva::gather::NodeState ns;
    ns.id = p.next_gather_node_id++;
    ns.node_config_path = cfg.config_path;
    ns.material_config_path = picked_material;
    ns.pos_x = x;
    ns.pos_y = selva::world::groundHeight(x, z);
    ns.pos_z = z;
    ns.quality = rollQuality(cfg);
    ns.yaw = frand(0.0f, 6.283185307179586f); // 0..2pi, committed at spawn
    p.active_gather_nodes.push_back(ns);
    return true;
}

// Called after loot::grantPickup has already done addItem + compendium
// + toast for the gather pickup. Our only responsibility: remove the
// NodeState from active_gather_nodes so the spawner's cap-check sees
// the population drop and trickles a replacement next interval.
void onNodeGranted(std::uint32_t node_id)
{
    selva::PlayerProfile* p = selva::activePlayerProfile();
    if (p == nullptr)
        return;
    auto& list = p->active_gather_nodes;
    list.erase(std::remove_if(list.begin(), list.end(), [node_id](const selva::gather::NodeState& s)
                              { return s.id == node_id; }),
               list.end());
    interactByNodeId().erase(node_id);
}

// Register a Pickup interactable for the given NodeState if one isn't
// already live. Uses loot::spawnPickup so the existing E-prompt /
// pickup-glow / inventory grant path all apply automatically. Reads
// the material from the NodeState (set at spawn time by rollWeightedPool),
// not the flow config -- one flow now produces many materials.
void ensureInteractable(const selva::gather::NodeState& ns)
{
    if (interactByNodeId().count(ns.id) > 0)
        return;
    if (ns.material_config_path.empty())
        return;

    engine::ecs::ItemInstance grant;
    grant.config_path = ns.material_config_path;
    grant.quality = ns.quality;
    grant.quantity = 1;

    const std::uint32_t node_id = ns.id;
    auto on_granted = [node_id]() { onNodeGranted(node_id); };

    const selva::loot::Id pickup_id =
        selva::loot::spawnPickup(glm::vec3(ns.pos_x, ns.pos_y, ns.pos_z), grant,
                                 /*source_actor_id*/ std::string{}, std::move(on_granted), ns.yaw);
    if (pickup_id == selva::loot::kInvalidId)
        return;
    // Track presence (any non-zero value) so we don't double-register
    // on subsequent ticks. The loot system owns the interactable's
    // lifetime; we just mark "this node has a live pickup."
    interactByNodeId()[node_id] = static_cast<selva::interact::Id>(pickup_id);
}

} // namespace

void initGatherSpawner()
{
    configs().clear();
    namespace fs = std::filesystem;
    const fs::path dir{"config/gather_nodes"};
    std::error_code ec;
    if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec))
    {
        std::fprintf(stderr, "[gather] config/gather_nodes not found; spawner inert\n");
        return;
    }
    for (const auto& entry : fs::directory_iterator(dir, ec))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".json")
            continue;
        NodeConfig nc;
        if (loadOneConfig(entry.path(), nc))
            configs().push_back(std::move(nc));
    }
    std::fprintf(stderr, "[gather] loaded %zu gather node configs\n", configs().size());
}

void tickGatherSpawner(float /*dt*/)
{
    selva::PlayerProfile* p = selva::activePlayerProfile();
    if (p == nullptr)
        return;

    // One-time forward migration: legacy saves (pre-unified-pool)
    // stored nodes with empty material_config_path. Drop them so the
    // spawner can refill from the new pool. New nodes always have a
    // committed material from rollWeightedPool.
    auto& nodes = p->active_gather_nodes;
    nodes.erase(std::remove_if(nodes.begin(), nodes.end(), [](const selva::gather::NodeState& s)
                               { return s.material_config_path.empty(); }),
                nodes.end());

    const float now = selva::wallClock();

    for (const auto& cfg : configs())
    {
        const int live = liveCountForConfig(*p, cfg);
        auto& flow = findOrCreateFlow(*p, cfg);
        if (live >= cfg.active_cap)
        {
            flow.initial_fill_done = true;
            continue;
        }
        // Two-phase: initial fill spawns at frame rate until cap reached
        // (so a fresh character sees the Wood populated within a second
        // or two of entering, not over many minutes). Once filled, the
        // trickle interval gates subsequent respawns (player gathered
        // one → wait respawn_seconds → spawn replacement).
        if (flow.initial_fill_done)
        {
            const float interval = resolveRespawnSeconds(cfg);
            if (now - flow.last_spawn_wallclock < interval)
                continue;
        }
        if (spawnOneNode(*p, cfg))
            flow.last_spawn_wallclock = now;
    }

    // Re-register interactables for every active node. Idempotent --
    // ensureInteractable no-ops if the node is already wired.
    for (const auto& ns : p->active_gather_nodes)
        ensureInteractable(ns);
}

void resetGatherSpawner()
{
    interactByNodeId().clear();
    selva::PlayerProfile* p = selva::activePlayerProfile();
    if (p == nullptr)
        return;
    p->active_gather_nodes.clear();
    p->gather_flows.clear();
    p->next_gather_node_id = 1;
}

} // namespace selva::gather
