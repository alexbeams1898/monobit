#include "systems/TravelSystem.h"

#include "AreaLoader.h"
#include "Engine.h"
#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "ops/LogUtils.h"
#include "systems/PlayerSystem.h"
#include "systems/TileMapRenderer.h"
#include "systems/WaveSystem.h"

#include <algorithm>
#include <unordered_map>
#include <vector>

#include <entt/entt.hpp>

namespace travel
{
namespace
{

// A door of the CURRENT area, in world space. The strip is the authored tile.
struct Door
{
    float x = 0.0f; // rect top-left
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    std::string id;
    std::string target;
};

constexpr float kFade = 0.35f; // seconds each way; the cut hides under it

std::string sWorldPath;                                  // the scanned .ldtk project
std::unordered_map<std::string, std::string> sDoorIndex; // door id -> level
std::vector<Door> sDoors;                                // doors of the current level
float sStartX = 0.0f; // where player_start put him, for doorless entries
float sStartY = 0.0f;
bool sHaveStart = false;
std::string sCurrent;

// The latch: disarmed on arrival, re-armed only once the body is clear of
// every door -- the doormat he lands on must not bounce him straight back.
bool sArmed = true;

enum class Phase
{
    None,
    FadeOut,
    FadeIn
};
Phase sPhase = Phase::None;
float sTimer = 0.0f;
std::string sPendingArea;
std::string sPendingDoor;

// The body's half-extents for door tests -- the foot box the world collides.
constexpr float kBodyHalfW = 8.0f;
constexpr float kBodyHalfH = 6.0f;

void collectDoor(const area::Object& o)
{
    Door d;
    d.x = o.x - o.w * 0.5f;
    d.y = o.y - o.h * 0.5f;
    d.w = o.w;
    d.h = o.h;
    d.id = o.props.value("id", std::string{});
    d.target = o.props.value("target", std::string{});
    if (d.id.empty() || d.target.empty())
    {
        poe::log().error("travel: a door needs both 'id' and 'target' ({}, {})", o.x, o.y);
        return;
    }
    sDoors.push_back(std::move(d));
}

// Everything except the player goes. His components -- stats, health, the
// pocket -- are him, and they walk through doors intact.
void clearWorld(EntityManager& em)
{
    auto& reg = em.registry();
    const entt::entity keep = player::entity();
    std::vector<entt::entity> gone;
    for (const auto e : reg.view<Transform>())
        if (e != keep)
            gone.push_back(e);
    for (const auto e : gone)
        reg.destroy(e);
}

void placePlayer(EntityManager& em, float x, float y)
{
    auto& reg = em.registry();
    const entt::entity p = player::entity();
    if (!reg.valid(p))
        return;
    auto& t = reg.get<Transform>(p);
    t.x = x;
    t.y = y;
    // History too, all of it -- a teleport is a cut, and any stale previous
    // value smears one frame of the old place across the new one.
    reg.get<PreviousTransform>(p) = PreviousTransform{x, y};
    if (auto* cam = reg.try_get<Camera>(p))
    {
        cam->x = x;
        cam->y = y;
        cam->prev_x = x;
        cam->prev_y = y;
    }
}

bool applyArea(Engine& engine, EntityManager& em, const area::Data& d, const std::string& doorId)
{
    auto& reg = em.registry();
    const entt::entity p = player::entity();
    const float px = reg.valid(p) ? reg.get<Transform>(p).x : 0.0f;
    const float py = reg.valid(p) ? reg.get<Transform>(p).y : 0.0f;

    sDoors.clear();
    sHaveStart = false;
    clearWorld(em);
    if (!area::build(em, d))
        return false;
    TileMapRenderer::upload(em.tile_map, em.tile_config, engine.textureManager());
    em.flow_field.last_player_col = -1;
    em.flow_field.last_player_row = -1;
    swarm::begin("config/swarm.json", {}, 0); // authored places press nothing on him -- yet

    float ax = sHaveStart ? sStartX : px;
    float ay = sHaveStart ? sStartY : py;
    if (!doorId.empty())
    {
        const auto it = std::find_if(sDoors.begin(), sDoors.end(),
                                     [&doorId](const Door& dr) { return dr.id == doorId; });
        if (it != sDoors.end())
        {
            ax = it->x + it->w * 0.5f;
            ay = it->y + it->h * 0.5f;
        }
        else
            poe::log().error("travel: '{}' has no door '{}' -- using its start", d.name, doorId);
    }
    placePlayer(em, ax, ay);
    sArmed = false; // he is standing on the doormat; clear of it re-arms
    sCurrent = d.name;
    poe::log().info("travel: entered '{}' at ({:.0f},{:.0f})", d.name, ax, ay);
    return true;
}

} // namespace

void init()
{
    area::registerBuilder("door",
                          [](EntityManager& /*em*/, const area::Object& o) { collectDoor(o); });
    area::registerBuilder("player_start",
                          [](EntityManager& /*em*/, const area::Object& o)
                          {
                              sStartX = o.x;
                              sStartY = o.y;
                              sHaveStart = true;
                          });
}

void scan(const std::string& ldtkPath)
{
    sWorldPath = ldtkPath;
    sDoorIndex.clear();
    for (const auto& lvl : area::levels(ldtkPath))
    {
        const area::Data d = area::loadLevel(ldtkPath, lvl);
        if (!d.ok)
            continue;
        for (const auto& o : d.objects)
        {
            if (o.type != "door")
                continue;
            const std::string id = o.props.value("id", std::string{});
            if (id.empty())
                continue;
            if (const auto [it, inserted] = sDoorIndex.emplace(id, lvl); !inserted)
                poe::log().error("travel: door id '{}' declared in both '{}' and '{}'", id,
                                 it->second, lvl);
        }
    }
    poe::log().info("travel: {} door(s) indexed", sDoorIndex.size());
}

bool enter(Engine& engine, EntityManager& em, const std::string& level,
           const std::string& arriveAtDoor)
{
    const area::Data d = area::loadLevel(sWorldPath, level);
    if (!d.ok)
        return false; // the world he is standing in stays as it was
    return applyArea(engine, em, d, arriveAtDoor);
}

void update(Engine& engine, EntityManager& em, float dt)
{
    if (sPhase == Phase::FadeOut)
    {
        sTimer += dt;
        if (sTimer >= kFade)
        {
            // Full black: the safe point. Nothing is mid-tick and nothing shows.
            if (!enter(engine, em, sPendingArea, sPendingDoor))
                poe::log().error("travel: door '{}' leads nowhere", sPendingDoor);
            sPhase = Phase::FadeIn;
            sTimer = 0.0f;
        }
        return;
    }
    if (sPhase == Phase::FadeIn)
    {
        sTimer += dt;
        if (sTimer >= kFade)
            sPhase = Phase::None;
        return;
    }

    if (sDoors.empty())
        return;
    auto& reg = em.registry();
    const entt::entity p = player::entity();
    if (!reg.valid(p))
        return;
    const auto& t = reg.get<Transform>(p);
    const auto& prev = reg.get<PreviousTransform>(p);

    if (!sArmed)
    {
        // Re-arm only once the body is clear of every door.
        bool onAny = false;
        for (const auto& d : sDoors)
            if (t.x + kBodyHalfW > d.x && t.x - kBodyHalfW < d.x + d.w && t.y + kBodyHalfH > d.y &&
                t.y - kBodyHalfH < d.y + d.h)
                onAny = true;
        sArmed = !onAny;
        return;
    }

    for (const auto& d : sDoors)
    {
        if (!sweptHit(prev.x, prev.y, t.x, t.y, kBodyHalfW, kBodyHalfH, d.x, d.y, d.w, d.h))
            continue;
        const auto it = sDoorIndex.find(d.target);
        if (it == sDoorIndex.end())
        {
            poe::log().error("travel: door '{}' targets unknown door '{}'", d.id, d.target);
            return;
        }
        sPendingArea = it->second;
        sPendingDoor = d.target;
        sPhase = Phase::FadeOut;
        sTimer = 0.0f;
        return;
    }
}

float curtainAlpha()
{
    if (sPhase == Phase::FadeOut)
        return std::min(1.0f, sTimer / kFade);
    if (sPhase == Phase::FadeIn)
        return 1.0f - std::min(1.0f, sTimer / kFade);
    return 0.0f;
}

bool active()
{
    return sPhase != Phase::None;
}

const std::string& currentArea()
{
    return sCurrent;
}

std::vector<std::string> doorIds()
{
    std::vector<std::string> ids;
    ids.reserve(sDoorIndex.size());
    for (const auto& [id, file] : sDoorIndex)
        ids.push_back(id);
    std::sort(ids.begin(), ids.end());
    return ids;
}

bool jumpToDoor(Engine& engine, EntityManager& em, const std::string& doorId)
{
    const auto it = sDoorIndex.find(doorId);
    if (it == sDoorIndex.end())
        return false;
    return enter(engine, em, it->second, doorId);
}

void reset()
{
    sDoors.clear();
    sCurrent.clear();
    sHaveStart = false;
    sArmed = true;
    sPhase = Phase::None;
    sTimer = 0.0f;
}

bool sweptHit(float prevX, float prevY, float curX, float curY, float halfW, float halfH, float rx,
              float ry, float rw, float rh)
{
    // Slab test of the segment against the rect grown by the body's half
    // extents -- a fast tick cannot step over a thin strip the way a point
    // sample would.
    const float minX = rx - halfW;
    const float minY = ry - halfH;
    const float maxX = rx + rw + halfW;
    const float maxY = ry + rh + halfH;
    const float dx = curX - prevX;
    const float dy = curY - prevY;

    float t0 = 0.0f;
    float t1 = 1.0f;
    const auto clip = [&t0, &t1](float p, float q)
    {
        // p = -d (entering) or d (leaving); q = distance to the slab plane.
        if (p == 0.0f)
            return q >= 0.0f; // parallel: inside the slab or never
        const float r = q / p;
        if (p < 0.0f)
            t0 = std::max(t0, r);
        else
            t1 = std::min(t1, r);
        return t0 <= t1;
    };
    return clip(-dx, prevX - minX) && clip(dx, maxX - prevX) && clip(-dy, prevY - minY) &&
           clip(dy, maxY - prevY);
}

} // namespace travel
