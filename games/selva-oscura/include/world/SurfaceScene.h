#pragma once

#include "world/Scene.h"

namespace selva::world
{

// The Selva surface (outdoor): wake-zone basin + colle + chapel
// exterior + trees. Wraps the LEGACY init path because today's
// terrain/static-mesh/physics state lives in global singletons that
// are initialized at boot.
//
// Future scenes that don't need terrain (chapel_interior, acheron)
// use JsonScene + scene.json instead. Once the singletons (terrain,
// chapel mesh) are refactored to scene-local state, SurfaceScene
// becomes a JsonScene too.
//
// onActivate: register the existing terrain + chapel + trees as
// Jolt static bodies (via the legacy initPhysicsScene path) and
// record handles with the activation context.
// onDeactivate: remove bodies (automatic via context tracking).
class SurfaceScene : public engine::world::Scene
{
public:
    SurfaceScene();
    void onActivate(engine::world::SceneActivationContext& ctx) override;
    void onDeactivate() override;
};

} // namespace selva::world
