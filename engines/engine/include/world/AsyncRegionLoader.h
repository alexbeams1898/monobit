#pragma once

#include "world/Region.h"

#include <atomic>
#include <future>
#include <memory>

namespace engine::world
{

// Async region loader.
//
// A Region that supports async preparation provides a `prepareAsync()`
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
// JsonRegion implements both phases. A region that doesn't need async
// can simply do nothing in prepareAsync() and all the work in
// commitPrepared() (which is what activateRegionImmediate does for
// the initial region at boot).

class AsyncCapableRegion : public Region
{
  public:
    using Region::Region;

    // Worker-thread phase. Read .glb files, build CPU vertex arrays,
    // create JPH::MeshShape instances. Store results in `this` for
    // commitPrepared to consume. MUST NOT touch the live PhysicsSystem
    // (no AddBody, no BodyInterface calls).
    virtual void prepareAsync() = 0;

    // Main-thread phase. Insert the prepared bodies into Jolt via
    // BodyInterface, record their handles into the region's owned list
    // via the context.
    virtual void commitPrepared(RegionActivationContext& ctx) = 0;

    // Legacy synchronous path: subclass can implement onActivate as
    // {prepareAsync(); commitPrepared(ctx);} for the immediate path
    // (boot region, tests). Default does exactly that.
    void onActivate(RegionActivationContext& ctx) override
    {
        prepareAsync();
        commitPrepared(ctx);
    }
};

// Kick off async preparation of `target` on a worker. Returns a
// future the RegionManager polls; when ready, the manager advances
// from LoadingTarget to FadingOut and ultimately Committing (which
// calls commitPrepared on the main thread).
std::future<void> beginAsyncRegionPrepare(AsyncCapableRegion& target);

} // namespace engine::world
