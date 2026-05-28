#pragma once

#include "world/Scene.h"

#include <atomic>
#include <future>
#include <memory>

namespace engine::world
{

// Async scene loader.
//
// A Scene that supports async preparation provides a `prepareAsync()`
// method that runs on a worker thread and prepares everything that
// CAN be done off the main thread (file I/O, Jolt MeshShape::Create,
// vertex array prep). The main thread, on commit frame, calls
// `commitPrepared()` which inserts the prepared shapes as Jolt
// bodies and registers triggers.
//
// Real-physics doctrine: body insertion stays on the main thread
// because Jolt's BodyInterface requires single-threaded calls during
// PhysicsSystem::Update. Shape creation IS thread-safe per Jolt docs
// and is by far the slow part for large meshes.
//
// JsonScene implements both phases. A scene that doesn't need async
// can simply do nothing in prepareAsync() and all the work in
// commitPrepared() (which is what activateSceneImmediate does for
// the initial scene at boot).

class AsyncCapableScene : public Scene
{
  public:
    using Scene::Scene;

    // Worker-thread phase. Read .glb files, build CPU vertex arrays,
    // create JPH::MeshShape instances. Store results in `this` for
    // commitPrepared to consume. MUST NOT touch the live PhysicsSystem
    // (no AddBody, no BodyInterface calls).
    virtual void prepareAsync() = 0;

    // Main-thread phase. Insert the prepared bodies into Jolt via
    // BodyInterface, record their handles into the scene's owned list
    // via the context.
    virtual void commitPrepared(SceneActivationContext& ctx) = 0;

    // Legacy synchronous path: subclass can implement onActivate as
    // {prepareAsync(); commitPrepared(ctx);} for the immediate path
    // (boot scene, tests). Default does exactly that.
    void onActivate(SceneActivationContext& ctx) override
    {
        prepareAsync();
        commitPrepared(ctx);
    }
};

// Kick off async preparation of `target` on a worker. Returns a
// future the SceneManager polls; when ready, the manager advances
// from LoadingTarget to FadingOut and ultimately Committing (which
// calls commitPrepared on the main thread).
std::future<void> beginAsyncScenePrepare(AsyncCapableScene& target);

} // namespace engine::world
