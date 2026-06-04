#include "world/Region.h"

#include "world/AsyncRegionLoader.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_map>

namespace engine::world
{

// ---------------------------------------------------------------------------
// RegionActivationContext — passes region's bodies/triggers into engine
// tracking. Diamond invariant: regions never touch their owned-body list
// directly; they go through the context.
// ---------------------------------------------------------------------------

void RegionActivationContext::addBody(engine::physics::BodyHandle h)
{
    if (h == engine::physics::kInvalidBody)
        return;
    region_ref.engineAppendBody(h);
}

void RegionActivationContext::addBodies(const std::vector<engine::physics::BodyHandle>& hs)
{
    for (auto h : hs)
        addBody(h);
}

void RegionActivationContext::addTrigger(const RegionTrigger& t)
{
    region_ref.engineAppendTrigger(t);
}

// ---------------------------------------------------------------------------
// RegionManager state
// ---------------------------------------------------------------------------

namespace
{
struct ManagerState
{
    std::vector<std::unique_ptr<Region>> regions; // owned, index = (RegionId-1)
    RegionId current = kInvalidRegion;
    PersistentRegionState persistent;

    // Transition state machine
    TransitionState state = TransitionState::Idle;
    RegionId target = kInvalidRegion;
    TransitionMode mode = TransitionMode::Fade;
    bool preserve_player_pos = false;
    glm::vec3 target_spawn_pos{0.0f};
    bool override_yaw = false;
    float target_yaw = 0.0f;
    float fade_duration = 0.4f;
    float fade_elapsed = 0.0f;
    float fade_alpha = 0.0f;

    // Edge-trigger tracking: which trigger (if any) the player was
    // inside last frame. Used to fire on entry-edge only, not every
    // frame inside.
    std::string player_inside_trigger_id; // empty = inside no trigger
    RegionId player_inside_trigger_region = kInvalidRegion;

    // Post-commit callback (game-side teleport hook).
    PostCommitCallback post_commit = nullptr;
};
ManagerState g;
} // namespace

// ---- Registration / lookup -----------------------------------------------

RegionId registerRegion(std::unique_ptr<Region> s)
{
    g.regions.push_back(std::move(s));
    return RegionId{static_cast<std::uint32_t>(g.regions.size())};
}

RegionId findRegionId(const char* region_id)
{
    if (!region_id)
        return kInvalidRegion;
    for (std::size_t i = 0; i < g.regions.size(); ++i)
    {
        if (g.regions[i]->regionId() == region_id)
            return RegionId{static_cast<std::uint32_t>(i + 1)};
    }
    return kInvalidRegion;
}

static Region* regionFromId(RegionId id)
{
    if (id.id == 0 || id.id > g.regions.size())
        return nullptr;
    return g.regions[id.id - 1].get();
}

int regionCount()
{
    return static_cast<int>(g.regions.size());
}
RegionId regionAt(int idx)
{
    return RegionId{static_cast<std::uint32_t>(idx + 1)};
}
Region* regionPtr(RegionId id)
{
    return regionFromId(id);
}
RegionId currentRegion()
{
    return g.current;
}
Region* currentRegionPtr()
{
    return regionFromId(g.current);
}

// ---- Activation / deactivation -------------------------------------------

// Internal: swap g.current to a different region, WITHOUT touching
// any region's bodies (multi-resident architecture: every region's
// bodies + meshes are resident at all times for zero-delay seamless
// traversal). The trigger-edge state is cleared because we're now
// in a different region's trigger frame.
static void swapCurrentTo(RegionId id)
{
    g.current = id;
    g.player_inside_trigger_id.clear();
    g.player_inside_trigger_region = kInvalidRegion;
}

// Internal: insert this region's bodies + triggers into the engine.
// Called ONCE per region at boot (multi-resident architecture). Per
// pillar 9 (preload-everything-before-main-menu), assets are already
// loaded; this just registers the prepared bodies + triggers.
//
// onActivate() runs commitPrepared() for AsyncCapableRegions (which
// inserts preloaded shapes), or the region's full onActivate for
// non-async types.
static void makeRegionResident(RegionId id)
{
    Region* s = regionFromId(id);
    if (s == nullptr)
    {
        std::fprintf(stderr, "[region-manager] makeResident: unknown RegionId=%u\n", id.id);
        return;
    }
    if (!s->ownedBodies().empty())
    {
        // Already resident (idempotent guard for any caller that
        // accidentally double-activates).
        return;
    }
    RegionActivationContext ctx(*s);
    s->onActivate(ctx);
    std::fprintf(stderr, "[region-manager] region '%s' resident (%zu bodies, %zu triggers)\n",
                 s->regionId().c_str(), s->ownedBodies().size(), s->triggers().size());
}

void activateRegionImmediate(RegionId id)
{
    // Multi-resident: ensure EVERY registered region is resident
    // (idempotent), then set the requested one as current. This is
    // called at boot exactly once -- subsequent transitions go
    // through beginTransition() which only swaps g.current via
    // swapCurrentTo() since all regions are already resident.
    for (int i = 0; i < regionCount(); ++i)
        makeRegionResident(regionAt(i));
    swapCurrentTo(id);
}

// ---- Transition state machine --------------------------------------------

bool beginTransition(RegionId target, TransitionMode mode, bool preserve_player_pos,
                     glm::vec3 target_spawn_pos, bool override_yaw, float target_yaw,
                     float fade_duration_seconds)
{
    if (g.state != TransitionState::Idle)
    {
        std::fprintf(stderr, "[region-manager] beginTransition: already in progress (state=%d)\n",
                     static_cast<int>(g.state));
        return false;
    }
    if (regionFromId(target) == nullptr)
    {
        std::fprintf(stderr, "[region-manager] beginTransition: unknown target RegionId=%u\n",
                     target.id);
        return false;
    }
    Region* tgt = regionFromId(target);
    std::fprintf(stderr,
                 "[region-manager] beginTransition: target='%s' RegionId=%u "
                 "spawn=(%.2f,%.2f,%.2f) override_yaw=%d yaw=%.2f mode=%d fade=%.2fs\n",
                 tgt->regionId().c_str(), target.id, target_spawn_pos.x, target_spawn_pos.y,
                 target_spawn_pos.z, override_yaw ? 1 : 0, target_yaw, static_cast<int>(mode),
                 fade_duration_seconds);
    g.target = target;
    g.mode = mode;
    g.preserve_player_pos = preserve_player_pos;
    g.target_spawn_pos = target_spawn_pos;
    g.override_yaw = override_yaw;
    g.target_yaw = target_yaw;
    g.fade_duration = fade_duration_seconds > 0.0f ? fade_duration_seconds : 0.4f;
    g.fade_elapsed = 0.0f;
    g.fade_alpha = 0.0f;
    // Per pillar 9 (preload-everything-before-main-menu): all regions
    // have their assets resident from boot, so there is no "load"
    // phase. Instant goes straight to Committing (single-frame body
    // swap). Fade plays a visual fade-to-black, then commits, then
    // fades in -- the fade is purely cosmetic, not loading cover.
    g.state = (mode == TransitionMode::Instant) ? TransitionState::Committing
                                                : TransitionState::FadingOut;
    std::fprintf(stderr, "[region-manager] -> state=%d\n", static_cast<int>(g.state));
    return true;
}

namespace
{
void tickFadingOut(float dt)
{
    g.fade_elapsed += dt;
    const float half = g.fade_duration * 0.5f;
    g.fade_alpha = std::min(1.0f, g.fade_elapsed / half);
    if (g.fade_elapsed >= half)
    {
        g.fade_alpha = 1.0f;
        g.state = TransitionState::Committing;
    }
}

void firePostCommit(bool preserve_pos, const glm::vec3& spawn_pos, bool override_yaw,
                    float spawn_yaw)
{
    if (g.post_commit != nullptr)
    {
        std::fprintf(stderr, "[region-manager] post-commit callback firing\n");
        g.post_commit(preserve_pos, spawn_pos, override_yaw, spawn_yaw);
    }
    else
    {
        std::fprintf(
            stderr,
            "[region-manager] WARNING: post_commit callback NULL — player will not teleport\n");
    }
}

void tickCommitting()
{
    const bool preserve_pos = g.preserve_player_pos;
    const glm::vec3 spawn_pos = g.target_spawn_pos;
    const bool override_yaw = g.override_yaw;
    const float spawn_yaw = g.target_yaw;
    std::fprintf(stderr,
                 "[region-manager] COMMIT: swapping current to RegionId=%u, "
                 "then teleporting player to (%.2f,%.2f,%.2f) override_yaw=%d\n",
                 g.target.id, spawn_pos.x, spawn_pos.y, spawn_pos.z, override_yaw ? 1 : 0);
    // Multi-resident: bodies + meshes are already in Jolt/GPU for
    // every region (since boot). Transition is just swapping which
    // region owns trigger-fire context + new-character spawn defaults.
    // Sub-millisecond -- no body insertion/removal.
    swapCurrentTo(g.target);
    g.target = kInvalidRegion;
    firePostCommit(preserve_pos, spawn_pos, override_yaw, spawn_yaw);
    if (g.mode == TransitionMode::Instant)
    {
        g.state = TransitionState::Idle;
        g.fade_alpha = 0.0f;
    }
    else
    {
        g.state = TransitionState::FadingIn;
        g.fade_elapsed = 0.0f;
    }
}

void tickFadingIn(float dt)
{
    g.fade_elapsed += dt;
    const float half = g.fade_duration * 0.5f;
    g.fade_alpha = std::max(0.0f, 1.0f - (g.fade_elapsed / half));
    if (g.fade_elapsed >= half)
    {
        g.fade_alpha = 0.0f;
        g.state = TransitionState::Idle;
    }
}
} // namespace

TransitionState tickRegionManager(float dt)
{
    const TransitionState entry_state = g.state;
    switch (g.state)
    {
    case TransitionState::Idle:
        break;
    case TransitionState::FadingOut:
        tickFadingOut(dt);
        break;
    case TransitionState::Committing:
        tickCommitting();
        break;
    case TransitionState::FadingIn:
        tickFadingIn(dt);
        break;
    }
    if (entry_state != g.state)
    {
        std::fprintf(stderr, "[region-manager] state %d -> %d (fade_alpha=%.2f)\n",
                     static_cast<int>(entry_state), static_cast<int>(g.state), g.fade_alpha);
    }
    return g.state;
}

TransitionState transitionState()
{
    return g.state;
}
float transitionFadeAlpha()
{
    return g.fade_alpha;
}
RegionId transitionTarget()
{
    return g.target;
}

// ---- Trigger overlap -----------------------------------------------------

static bool aabbContainsPoint(const glm::vec3& center, const glm::vec3& half, const glm::vec3& p)
{
    return std::abs(p.x - center.x) <= half.x && std::abs(p.y - center.y) <= half.y &&
           std::abs(p.z - center.z) <= half.z;
}

const RegionTrigger* checkPlayerTriggers(const glm::vec3& player_pos)
{
    Region* cur = currentRegionPtr();
    if (cur == nullptr)
        return nullptr;

    // Find the trigger (if any) the player is inside this frame.
    const RegionTrigger* hit = nullptr;
    for (const auto& t : cur->triggers())
    {
        if (aabbContainsPoint(t.center, t.half_extents, player_pos))
        {
            hit = &t;
            break;
        }
    }

    // Diagnostic: every ~60th frame (rough 1Hz at 60fps), dump player
    // pos + per-trigger distance from each AABB so we can see how close
    // we are to firing. Strip once trigger-fire bugs are gone.
    static int s_diag_tick = 0;
    if ((++s_diag_tick % 60) == 0)
    {
        for (const auto& t : cur->triggers())
        {
            const float dx = std::abs(player_pos.x - t.center.x) - t.half_extents.x;
            const float dy = std::abs(player_pos.y - t.center.y) - t.half_extents.y;
            const float dz = std::abs(player_pos.z - t.center.z) - t.half_extents.z;
            const bool inside = (dx <= 0.0f) && (dy <= 0.0f) && (dz <= 0.0f);
            std::fprintf(stderr,
                         "[trigger-diag] '%s' player=(%.2f,%.2f,%.2f) "
                         "aabb_center=(%.2f,%.2f,%.2f) half=(%.2f,%.2f,%.2f) "
                         "slack(x,y,z)=(%.2f,%.2f,%.2f) inside=%d\n",
                         t.id.c_str(), player_pos.x, player_pos.y, player_pos.z, t.center.x,
                         t.center.y, t.center.z, t.half_extents.x, t.half_extents.y,
                         t.half_extents.z, -dx, -dy, -dz, inside ? 1 : 0);
        }
        std::fflush(stderr);
    }

    // Edge detection: fire only when the player ENTERS a trigger
    // (was not inside one last frame, or was inside a different one).
    if (hit == nullptr)
    {
        // Player left whatever trigger they were in (if any).
        g.player_inside_trigger_id.clear();
        g.player_inside_trigger_region = kInvalidRegion;
        return nullptr;
    }

    const bool same_as_last =
        (g.player_inside_trigger_region == g.current) && (g.player_inside_trigger_id == hit->id);
    g.player_inside_trigger_id = hit->id;
    g.player_inside_trigger_region = g.current;
    if (same_as_last)
        return nullptr; // already-inside, no edge

    std::fprintf(
        stderr, "[region-manager] trigger ENTER '%s' (player at %.2f,%.2f,%.2f) in region '%s'\n",
        hit->id.c_str(), player_pos.x, player_pos.y, player_pos.z, cur->regionId().c_str());

    // Fresh edge. RegionTransition triggers queue a transition;
    // Custom triggers are returned to the caller (game) to dispatch.
    if (hit->action == TriggerAction::RegionTransition)
    {
        if (g.state == TransitionState::Idle && hit->target != kInvalidRegion)
        {
            beginTransition(hit->target, hit->mode, hit->preserve_player_pos, hit->target_spawn_pos,
                            hit->override_yaw, hit->target_yaw, hit->fade_duration_seconds);
        }
        else
        {
            std::fprintf(stderr, "[region-manager] trigger '%s' suppressed (state=%d, target=%u)\n",
                         hit->id.c_str(), static_cast<int>(g.state), hit->target.id);
        }
    }
    // Custom-action triggers fall through: caller inspects
    // hit->action and hit->action_payload, dispatches game-side.
    return hit;
}

void setPersistentState(const PersistentRegionState& s)
{
    g.persistent = s;
}
const PersistentRegionState& persistentState()
{
    return g.persistent;
}

void setPostCommitCallback(PostCommitCallback cb)
{
    g.post_commit = cb;
}

// ---- Bootstrap -----------------------------------------------------------

void initRegionManager()
{
    g = ManagerState{};
}

void shutdownRegionManager()
{
    // Multi-resident: every region's bodies are in Jolt. Remove each
    // region's bodies before clearing the region list (the Region's
    // dtor doesn't touch physics; we do that here explicitly).
    for (auto& s : g.regions)
    {
        if (s == nullptr)
            continue;
        for (auto h : s->ownedBodies())
            engine::physics::removeBody(h);
        s->engineClearOwnership();
        s->onDeactivate();
    }
    // Per-region shutdown for resident-all-regions asset freeing.
    for (auto& s : g.regions)
        if (s)
            s->onShutdown();
    g.regions.clear();
    g = ManagerState{};
}

} // namespace engine::world
