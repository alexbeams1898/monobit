#pragma once

#include "world/Region.h"

namespace selva::world
{

// The Selva surface (outdoor): wake-zone basin + colle + chapel
// exterior + trees. Wraps the LEGACY init path because today's
// terrain/static-mesh/physics state lives in global singletons that
// are initialized at boot.
//
// Future scenes that don't need terrain use JsonRegion + region.json
// instead. Once the singletons (terrain, chapel mesh) are refactored
// to scene-local state, SurfaceRegion becomes a JsonRegion too.
//
// onActivate: register the existing terrain + chapel + trees as
// Jolt static bodies (via the legacy initPhysicsRegion path) and
// record handles with the activation context.
// onDeactivate: remove bodies (automatic via context tracking).
class SurfaceRegion : public engine::world::Region
{
  public:
    SurfaceRegion();
    void onActivate(engine::world::RegionActivationContext& ctx) override;
    void onDeactivate() override;
};

} // namespace selva::world
