#include "systems/TravelSystem.h"

#include "formats/AreaLoader.h"
#include "Engine.h"
#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "ops/LogUtils.h"
#include "systems/PlayerSystem.h"
#include "systems/TileMapRenderer.h"
#include "systems/WaveSystem.h"

#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <vector>

#include <entt/entt.hpp>

namespace travel
{
namespace
{

// A warp of the CURRENT level, in world space -- a doorway, a staircase,
// whatever the tiles under it look like. The strip is the authored entity.
struct Warp
{
    float x = 0.0f; // rect top-left
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    std::string id;
    std::string target;
    std::string facing; // the cardinal you step out along when arriving here
};

// A facing name -> unit direction. Unknown/empty = (0,0), which gates nothing.
void facingVec(const std::string& facing, float& dx, float& dy)
{
    dx = 0.0f;
    dy = 0.0f;
    if (facing == "north")
        dy = -1.0f;
    else if (facing == "south")
        dy = 1.0f;
    else if (facing == "east")
        dx = 1.0f;
    else if (facing == "west")
        dx = -1.0f;
}

constexpr float kFade = 0.35f; // seconds each way; the cut hides under it

std::string sWorldPath;                                  // the scanned .ldtk project
std::unordered_map<std::string, std::string> sWarpIndex; // warp id -> level
std::vector<Warp> sWarps;                                // warps of the current level
float sStartX = 0.0f; // where player_start put him, for warpless entries
float sStartY = 0.0f;
bool sHaveStart = false;
std::string sCurrent;

// No arrival latch: the exit-line trigger makes bounce-back structurally
// impossible. Arriving, you stand on the FACING side of the strip -- holding
// your walk direction crosses the facing edge, which never fires; only a
// deliberate walk back out the far side does.
enum class Phase
{
    None,
    FadeOut,
    FadeIn
};
Phase sPhase = Phase::None;
float sTimer = 0.0f;
std::string sPendingArea;
std::string sPendingWarp;
// What to do at full black when the cut is not a door: a floor being dug, climbed out of, or
// whatever swaps the world next.
std::function<void()> sPendingAct;

void collectWarp(const area::Object& o)
{
    Warp w;
    w.x = o.x - o.w * 0.5f;
    w.y = o.y - o.h * 0.5f;
    w.w = o.w;
    w.h = o.h;
    w.id = o.props.value("id", std::string{});
    w.target = o.props.value("target", std::string{});
    // The map's Facing enum capitalizes (the editor's identifier rules);
    // normalized once, here, at the boundary.
    if (o.props.contains("facing") && o.props["facing"].is_string())
    {
        w.facing = o.props["facing"].get<std::string>();
        std::transform(w.facing.begin(), w.facing.end(), w.facing.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    }
    if (w.id.empty() || w.target.empty())
    {
        poe::log().error("travel: a warp needs both 'id' and 'target' ({}, {})", o.x, o.y);
        return;
    }
    if (w.facing != "north" && w.facing != "south" && w.facing != "east" && w.facing != "west")
    {
        poe::log().error("travel: warp '{}' needs a facing (north/south/east/west) -- refused",
                         w.id);
        return;
    }
    sWarps.push_back(std::move(w));
}

// Everything except the player goes. His components -- stats, health, the
// pocket -- are him, and they walk through warps intact.
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

// Warpless entry into a level with no start: the first open floor tile. Only
// dev jumps take this path -- play always arrives through a warp.
void firstWalkable(const EntityManager& em, float& x, float& y)
{
    const auto& map = em.tile_map;
    const auto ts = static_cast<float>(map.tile_size);
    for (int row = 0; row < map.height; ++row)
        for (int col = 0; col < map.width; ++col)
            if (map.at(col, row).walkable)
            {
                x = static_cast<float>(col) * ts + ts * 0.5f;
                y = static_cast<float>(row) * ts + ts * 0.5f;
                return;
            }
}

bool applyArea(Engine& engine, EntityManager& em, const area::Data& d, const std::string& warpId)
{
    sWarps.clear();
    sHaveStart = false;
    clearWorld(em);
    if (!area::build(em, d))
        return false;
    TileMapRenderer::upload(em.tile_map, em.tile_config, engine.textureManager());
    em.flow_field.last_player_col = -1;
    em.flow_field.last_player_row = -1;
    swarm::begin("config/swarm.json", {}, 0); // authored places press nothing on him -- yet

    float ax = sStartX;
    float ay = sStartY;
    if (!sHaveStart)
        firstWalkable(em, ax, ay);
    if (!warpId.empty())
    {
        const auto it = std::find_if(sWarps.begin(), sWarps.end(),
                                     [&warpId](const Warp& wr) { return wr.id == warpId; });
        if (it != sWarps.end())
        {
            ax = it->x + it->w * 0.5f;
            ay = it->y + it->h * 0.5f;
            // The mirror of the exit line: you vanished crossing the far
            // edge, so you appear HALFWAY into the facing side of the block,
            // still in the passage, stepping out of it.
            float fx = 0.0f;
            float fy = 0.0f;
            facingVec(it->facing, fx, fy);
            ax += fx * it->w * 0.25f;
            ay += fy * it->h * 0.25f;
        }
        else
            poe::log().error("travel: '{}' has no warp '{}' -- using its start", d.name, warpId);
    }
    placePlayer(em, ax, ay);
    sCurrent = d.name;
    poe::log().info("travel: entered '{}' at ({:.0f},{:.0f})", d.name, ax, ay);
    return true;
}

} // namespace

void init()
{
    area::registerBuilder("warp",
                          [](EntityManager& /*em*/, const area::Object& o) { collectWarp(o); });
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
    sWarpIndex.clear();
    for (const auto& lvl : area::levels(ldtkPath))
    {
        const area::Data d = area::loadLevel(ldtkPath, lvl);
        if (!d.ok)
            continue;
        for (const auto& o : d.objects)
        {
            if (o.type != "warp")
                continue;
            const std::string id = o.props.value("id", std::string{});
            if (id.empty())
                continue;
            if (const auto [it, inserted] = sWarpIndex.emplace(id, lvl); !inserted)
                poe::log().error("travel: warp id '{}' declared in both '{}' and '{}'", id,
                                 it->second, lvl);
        }
    }
    poe::log().info("travel: {} warp(s) indexed", sWarpIndex.size());
}

bool enter(Engine& engine, EntityManager& em, const std::string& level,
           const std::string& arriveAtWarp)
{
    const area::Data d = area::loadLevel(sWorldPath, level);
    if (!d.ok)
        return false; // the world he is standing in stays as it was
    return applyArea(engine, em, d, arriveAtWarp);
}

void update(Engine& engine, EntityManager& em, float dt)
{
    if (sPhase == Phase::FadeOut)
    {
        sTimer += dt;
        if (sTimer >= kFade)
        {
            // Full black: the safe point. Nothing is mid-tick and nothing shows.
            if (sPendingAct)
            {
                const std::function<void()> act = std::move(sPendingAct);
                sPendingAct = nullptr;
                act();
            }
            else if (!enter(engine, em, sPendingArea, sPendingWarp))
                poe::log().error("travel: warp '{}' leads nowhere", sPendingWarp);
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

    if (sWarps.empty())
        return;
    auto& reg = em.registry();
    const entt::entity p = player::entity();
    if (!reg.valid(p))
        return;
    const auto& t = reg.get<Transform>(p);
    const auto& prev = reg.get<PreviousTransform>(p);

    // The test runs on the INTENDED path: where he is stands plus where he is
    // pressing, carried half a tile. A wall can stop his feet short of a
    // threshold set into it, but not his intent to walk through.
    float ix = 0.0f;
    float iy = 0.0f;
    player::moveIntent(ix, iy);
    constexpr float kReach = 16.0f;
    const float ex = t.x + ix * kReach;
    const float ey = t.y + iy * kReach;

    for (const auto& d : sWarps)
    {
        if (!crossesExit(prev.x, prev.y, ex, ey, d.x, d.y, d.w, d.h, d.facing))
            continue;
        const auto it = sWarpIndex.find(d.target);
        if (it == sWarpIndex.end())
        {
            poe::log().error("travel: warp '{}' targets unknown warp '{}'", d.id, d.target);
            return;
        }
        sPendingArea = it->second;
        sPendingWarp = d.target;
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

void cut(std::function<void()> atBlack)
{
    if (sPhase != Phase::None)
        return; // one curtain at a time; a cut inside a cut is a dropped frame at best
    sPendingArea.clear();
    sPendingWarp.clear();
    sPendingAct = std::move(atBlack);
    sPhase = Phase::FadeOut;
    sTimer = 0.0f;
}

bool active()
{
    return sPhase != Phase::None;
}

const std::string& currentArea()
{
    return sCurrent;
}

std::vector<std::string> warpIds()
{
    std::vector<std::string> ids;
    ids.reserve(sWarpIndex.size());
    for (const auto& [id, file] : sWarpIndex)
        ids.push_back(id);
    std::sort(ids.begin(), ids.end());
    return ids;
}

bool jumpToWarp(Engine& engine, EntityManager& em, const std::string& warpId)
{
    const auto it = sWarpIndex.find(warpId);
    if (it == sWarpIndex.end())
        return false;
    return enter(engine, em, it->second, warpId);
}

void reset()
{
    sWarps.clear();
    sCurrent.clear();
    sHaveStart = false;
    sPhase = Phase::None;
    sTimer = 0.0f;
}

void leaveAuthored()
{
    sWarps.clear();
    sCurrent.clear();
    sHaveStart = false;
}

bool crossesExit(float prevX, float prevY, float curX, float curY, float rx, float ry, float rw,
                 float rh, const std::string& facing)
{
    float fx = 0.0f;
    float fy = 0.0f;
    facingVec(facing, fx, fy);
    if (fy != 0.0f)
    {
        // Facing north: you leave moving south, out through the south edge.
        const float line = (fy < 0.0f) ? ry + rh : ry;
        const bool crossed =
            (fy < 0.0f) ? (prevY < line && curY >= line) : (prevY > line && curY <= line);
        const float dy = curY - prevY;
        if (!crossed || dy == 0.0f)
            return false;
        const float x = prevX + (curX - prevX) * ((line - prevY) / dy);
        return x >= rx && x <= rx + rw;
    }
    if (fx != 0.0f)
    {
        const float line = (fx < 0.0f) ? rx + rw : rx;
        const bool crossed =
            (fx < 0.0f) ? (prevX < line && curX >= line) : (prevX > line && curX <= line);
        const float dx = curX - prevX;
        if (!crossed || dx == 0.0f)
            return false;
        const float y = prevY + (curY - prevY) * ((line - prevX) / dx);
        return y >= ry && y <= ry + rh;
    }
    return false; // unreachable through a loaded warp -- facings are required
}

} // namespace travel
