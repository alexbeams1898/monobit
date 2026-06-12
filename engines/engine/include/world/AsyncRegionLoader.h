#pragma once

#include "world/Region.h"

namespace engine::world
{

// Resident-region helper.
//
// Originally this class supported a two-phase activate (worker-thread
// prepareAsync + main-thread commitPrepared). The async path was
// retired when the engine adopted pillar 9's preload-everything-
// before-main-menu doctrine: every region's heavy work (.glb reads,
// JPH::MeshShape::Create, CPU vertex array prep) runs once at boot
// via the subclass's own preload entry point, and only the cheap
// body-insert remains per-activation. The class stays for the clear
// onActivate -> commitPrepared shape it gives subclasses.
//
// JsonRegion is the only implementer today.
class AsyncCapableRegion : public Region
{
  public:
    using Region::Region;

    // Main-thread phase: insert preloaded shapes as Jolt bodies and
    // register triggers via the context. Sub-millisecond because
    // shapes are already built; nothing to load here.
    virtual void commitPrepared(RegionActivationContext& ctx) = 0;

    void onActivate(RegionActivationContext& ctx) override
    {
        commitPrepared(ctx);
    }
};

} // namespace engine::world
