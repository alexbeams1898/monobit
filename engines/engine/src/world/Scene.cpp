#include "world/Scene.h"

#include "world/AsyncSceneLoader.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <future>
#include <unordered_map>

namespace engine::world
{

// ---------------------------------------------------------------------------
// SceneActivationContext — passes scene's bodies/triggers into engine
// tracking. Diamond invariant: scenes never touch their owned-body list
// directly; they go through the context.
// ---------------------------------------------------------------------------

void SceneActivationContext::addBody(engine::physics::BodyHandle h)
{
    if (h == engine::physics::kInvalidBody)
        return;
    scene_ref.engineAppendBody(h);
}

void SceneActivationContext::addBodies(const std::vector<engine::physics::BodyHandle>& hs)
{
    for (auto h : hs)
        addBody(h);
}

void SceneActivationContext::addTrigger(const SceneTrigger& t)
{
    scene_ref.engineAppendTrigger(t);
}

// ---------------------------------------------------------------------------
// SceneManager state
// ---------------------------------------------------------------------------

namespace
{
struct ManagerState
{
    std::vector<std::unique_ptr<Scene>> scenes; // owned, index = (SceneId-1)
    SceneId current = kInvalidScene;
    PersistentSceneState persistent;

    // Transition state machine
    TransitionState state = TransitionState::Idle;
    SceneId target = kInvalidScene;
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
    SceneId player_inside_trigger_scene = kInvalidScene;

    // Async loading state. Populated when a transition begins; polled
    // in tickSceneManager. When the future is ready, we advance from
    // LoadingTarget to FadingOut.
    std::future<void> load_future;
    bool load_in_flight = false;

    // Post-commit callback (game-side teleport hook).
    PostCommitCallback post_commit = nullptr;
};
ManagerState g;
} // namespace

// ---- Registration / lookup -----------------------------------------------

SceneId registerScene(std::unique_ptr<Scene> s)
{
    g.scenes.push_back(std::move(s));
    return SceneId{static_cast<std::uint32_t>(g.scenes.size())};
}

SceneId findSceneId(const char* scene_id)
{
    if (!scene_id)
        return kInvalidScene;
    for (std::size_t i = 0; i < g.scenes.size(); ++i)
    {
        if (g.scenes[i]->sceneId() == scene_id)
            return SceneId{static_cast<std::uint32_t>(i + 1)};
    }
    return kInvalidScene;
}

static Scene* sceneFromId(SceneId id)
{
    if (id.id == 0 || id.id > g.scenes.size())
        return nullptr;
    return g.scenes[id.id - 1].get();
}

int sceneCount()
{
    return static_cast<int>(g.scenes.size());
}
SceneId sceneAt(int idx)
{
    return SceneId{static_cast<std::uint32_t>(idx + 1)};
}
Scene* scenePtr(SceneId id)
{
    return sceneFromId(id);
}
SceneId currentScene()
{
    return g.current;
}
Scene* currentScenePtr()
{
    return sceneFromId(g.current);
}

// ---- Activation / deactivation -------------------------------------------

// Internal: deactivate the current scene, removing every body it owns.
static void deactivateCurrent()
{
    Scene* cur = currentScenePtr();
    if (cur == nullptr)
        return;
    // Diamond cleanup: every body the scene declared via the context
    // is removed. Scene cannot leak.
    for (auto h : cur->ownedBodies())
        engine::physics::removeBody(h);
    cur->engineClearOwnership();
    cur->onDeactivate();
    g.current = kInvalidScene;
    g.player_inside_trigger_id.clear();
    g.player_inside_trigger_scene = kInvalidScene;
}

// Internal: activate a scene. `from_async` = the worker has already
// completed prepareAsync; we only need to commit on main thread. Else
// it's the immediate path (boot, tests) and we run the full path.
static void activateInternal(SceneId id, bool from_async)
{
    Scene* s = sceneFromId(id);
    if (s == nullptr)
    {
        std::fprintf(stderr, "[scene-manager] activate: unknown SceneId=%u\n", id.id);
        return;
    }
    SceneActivationContext ctx(*s);
    auto* async_s = dynamic_cast<AsyncCapableScene*>(s);
    if (from_async && async_s != nullptr)
        async_s->commitPrepared(ctx);
    else
        s->onActivate(ctx);
    g.current = id;
    std::fprintf(stderr, "[scene-manager] activated '%s' (%zu bodies, %zu triggers)\n",
                 s->sceneId().c_str(), s->ownedBodies().size(), s->triggers().size());
}

void activateSceneImmediate(SceneId id)
{
    deactivateCurrent();
    activateInternal(id, /*from_async=*/false);
}

// ---- Transition state machine --------------------------------------------

bool beginTransition(SceneId target, TransitionMode mode, bool preserve_player_pos,
                     glm::vec3 target_spawn_pos, bool override_yaw, float target_yaw,
                     float fade_duration_seconds)
{
    if (g.state != TransitionState::Idle)
    {
        std::fprintf(stderr, "[scene-manager] beginTransition: already in progress (state=%d)\n",
                     static_cast<int>(g.state));
        return false;
    }
    if (sceneFromId(target) == nullptr)
    {
        std::fprintf(stderr, "[scene-manager] beginTransition: unknown target SceneId=%u\n",
                     target.id);
        return false;
    }
    Scene* tgt = sceneFromId(target);
    std::fprintf(stderr,
                 "[scene-manager] beginTransition: target='%s' SceneId=%u "
                 "spawn=(%.2f,%.2f,%.2f) override_yaw=%d yaw=%.2f mode=%d fade=%.2fs\n",
                 tgt->sceneId().c_str(), target.id, target_spawn_pos.x, target_spawn_pos.y,
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
    g.state = (mode == TransitionMode::Instant) ? TransitionState::Committing
                                                : TransitionState::LoadingTarget;
    std::fprintf(stderr, "[scene-manager] -> state=%d\n", static_cast<int>(g.state));
    return true;
}

namespace
{
void tickLoadingTarget()
{
    Scene* tgt = sceneFromId(g.target);
    auto* async_tgt = dynamic_cast<AsyncCapableScene*>(tgt);
    if (async_tgt != nullptr && !g.load_in_flight)
    {
        std::fprintf(stderr, "[scene-manager] kicking async prepare for '%s'\n",
                     tgt->sceneId().c_str());
        g.load_future = beginAsyncScenePrepare(*async_tgt);
        g.load_in_flight = true;
    }
    bool ready = !g.load_in_flight;
    if (g.load_in_flight && g.load_future.valid())
        ready = (g.load_future.wait_for(std::chrono::seconds(0)) == std::future_status::ready);
    if (!ready)
        return;
    if (g.load_in_flight)
    {
        g.load_future.get(); // surface exceptions
        std::fprintf(stderr, "[scene-manager] async prepare ready\n");
    }
    g.load_in_flight = false;
    g.state = TransitionState::FadingOut;
    g.fade_elapsed = 0.0f;
}

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
        std::fprintf(stderr, "[scene-manager] post-commit callback firing\n");
        g.post_commit(preserve_pos, spawn_pos, override_yaw, spawn_yaw);
    }
    else
    {
        std::fprintf(
            stderr,
            "[scene-manager] WARNING: post_commit callback NULL — player will not teleport\n");
    }
}

void tickCommitting()
{
    const bool from_async = (g.mode != TransitionMode::Instant);
    const bool preserve_pos = g.preserve_player_pos;
    const glm::vec3 spawn_pos = g.target_spawn_pos;
    const bool override_yaw = g.override_yaw;
    const float spawn_yaw = g.target_yaw;
    std::fprintf(stderr,
                 "[scene-manager] COMMIT: deactivating + activating target SceneId=%u "
                 "(from_async=%d), then teleporting player to (%.2f,%.2f,%.2f) "
                 "override_yaw=%d\n",
                 g.target.id, from_async ? 1 : 0, spawn_pos.x, spawn_pos.y, spawn_pos.z,
                 override_yaw ? 1 : 0);
    deactivateCurrent();
    activateInternal(g.target, from_async);
    g.target = kInvalidScene;
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

TransitionState tickSceneManager(float dt)
{
    const TransitionState entry_state = g.state;
    switch (g.state)
    {
    case TransitionState::Idle:
        break;
    case TransitionState::LoadingTarget:
        tickLoadingTarget();
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
        std::fprintf(stderr, "[scene-manager] state %d -> %d (fade_alpha=%.2f)\n",
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
SceneId transitionTarget()
{
    return g.target;
}

// ---- Trigger overlap -----------------------------------------------------

static bool aabbContainsPoint(const glm::vec3& center, const glm::vec3& half, const glm::vec3& p)
{
    return std::abs(p.x - center.x) <= half.x && std::abs(p.y - center.y) <= half.y &&
           std::abs(p.z - center.z) <= half.z;
}

const SceneTrigger* checkPlayerTriggers(const glm::vec3& player_pos)
{
    Scene* cur = currentScenePtr();
    if (cur == nullptr)
        return nullptr;

    // Find the trigger (if any) the player is inside this frame.
    const SceneTrigger* hit = nullptr;
    for (const auto& t : cur->triggers())
    {
        if (aabbContainsPoint(t.center, t.half_extents, player_pos))
        {
            hit = &t;
            break;
        }
    }

    // Edge detection: fire only when the player ENTERS a trigger
    // (was not inside one last frame, or was inside a different one).
    if (hit == nullptr)
    {
        // Player left whatever trigger they were in (if any).
        g.player_inside_trigger_id.clear();
        g.player_inside_trigger_scene = kInvalidScene;
        return nullptr;
    }

    const bool same_as_last =
        (g.player_inside_trigger_scene == g.current) && (g.player_inside_trigger_id == hit->id);
    g.player_inside_trigger_id = hit->id;
    g.player_inside_trigger_scene = g.current;
    if (same_as_last)
        return nullptr; // already-inside, no edge

    std::fprintf(stderr,
                 "[scene-manager] trigger ENTER '%s' (player at %.2f,%.2f,%.2f) in scene '%s'\n",
                 hit->id.c_str(), player_pos.x, player_pos.y, player_pos.z, cur->sceneId().c_str());

    // Fresh edge: queue the transition.
    if (g.state == TransitionState::Idle && hit->target != kInvalidScene)
    {
        beginTransition(hit->target, hit->mode, hit->preserve_player_pos, hit->target_spawn_pos,
                        hit->override_yaw, hit->target_yaw, hit->fade_duration_seconds);
    }
    else
    {
        std::fprintf(stderr, "[scene-manager] trigger '%s' suppressed (state=%d, target=%u)\n",
                     hit->id.c_str(), static_cast<int>(g.state), hit->target.id);
    }
    return hit;
}

void setPersistentState(const PersistentSceneState& s)
{
    g.persistent = s;
}
const PersistentSceneState& persistentState()
{
    return g.persistent;
}

void setPostCommitCallback(PostCommitCallback cb)
{
    g.post_commit = cb;
}

// ---- Bootstrap -----------------------------------------------------------

void initSceneManager()
{
    g = ManagerState{};
}

void shutdownSceneManager()
{
    deactivateCurrent();
    // Per-scene shutdown for resident-all-scenes asset freeing.
    for (auto& s : g.scenes)
        if (s)
            s->onShutdown();
    g.scenes.clear();
    g = ManagerState{};
}

} // namespace engine::world
