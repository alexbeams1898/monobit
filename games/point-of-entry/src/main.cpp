#include "Capture.h"
#include "Engine.h"
#include "FloorGen.h"
#include "Log.h"
#include "Player.h"
#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "systems/RenderSystem.h"
#include "systems/TileMapRenderer.h"

#include <glad/glad.h>

// Point of Entry -- the skeleton. Right now it does one thing: build a floor out
// of hand-authored ASCII rooms, put the player in it, and let him walk around.
// Everything else (the gadget, sealing, the swarm) arrives on top of this.

namespace
{
// Placeholder art: the engine draws a coloured quad for a sprite with no
// texture, which is all this needs until the real pixel art exists.
constexpr float kPlayerSize = 24.0f;
constexpr float kPoeSize = 28.0f;

// A box that reads as a thing, with no art yet.
entt::entity spawnBox(EntityManager& em, float x, float y, float size, float r, float g, float b)
{
    auto& reg = em.registry();
    const entt::entity e = reg.create();
    // Transform is {x, y, z, rotation, pitch, roll, scale} -- set the fields by
    // name rather than by position, or a mis-counted brace silently lands in the
    // wrong one (scale 0 draws a sprite zero pixels wide, which looks exactly
    // like the sprite not existing).
    Transform t{};
    t.x = x;
    t.y = y;
    reg.emplace<Transform>(e, t);
    reg.emplace<PreviousTransform>(e, PreviousTransform{x, y});
    Sprite spr{};
    spr.src_w = static_cast<int>(size);
    spr.src_h = static_cast<int>(size);
    spr.layer = 2;
    reg.emplace<Sprite>(e, spr);
    // No art yet: the renderer draws a flat quad for a SolidColor sprite, which
    // is all the skeleton needs to prove placement.
    reg.emplace<SolidColor>(e, SolidColor{r, g, b});
    return e;
}

void gameRenderWorld(Engine& engine, EntityManager& em, float camX, float camY, float /*alpha*/)
{
    const float zoom = engine.cameraZoom();
    TileMapRenderer::render(camX, camY, engine.windowWidth(), engine.windowHeight(), zoom);
    RenderSystem::render(em, engine.textureManager(), camX, camY, zoom);

    capture::writeIfRequested(engine.windowWidth(), engine.windowHeight());
}

void gameOnResize(Engine& /*engine*/, int w, int h)
{
    RenderSystem::resize(w, h);
}
} // namespace

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    Engine engine;
    if (!engine.init("Point of Entry", 1280, 720))
        return 1;

    poe::log().info("boot: window is {}x{}", engine.windowWidth(), engine.windowHeight());
    RenderSystem::init(engine.windowWidth(), engine.windowHeight());
    TileMapRenderer::init();
    // The engine enables depth testing globally at init for the 3D path. This is a 2D
    // game: everything draws at z=0, so with GL_LESS the tilemap writes depth first and
    // every sprite fails the test and vanishes. Depth order here is the painter's
    // algorithm (RenderSystem sorts by Y), so the test must be off.
    //
    // Worth knowing: the other 2D games only avoid this by ACCIDENT -- UIRenderer
    // disables depth when it submits a batch, and they draw a HUD every frame. A game
    // with no HUD yet hits it immediately, and every diagnostic still reads as correct.
    glDisable(GL_DEPTH_TEST);

    auto& em = engine.entityManager();

    const floorgen::Floor floor = floorgen::generate(em, "config/floor.json", "config/rooms");
    if (!floor.ok)
    {
        poe::log().error("boot: could not build a floor -- nothing to walk around in");
        return 1;
    }
    TileMapRenderer::upload(em.tile_map, em.tile_config, engine.textureManager());

    // Every marker the rooms carried. 'P' is a point of entry -- for now it is a
    // red box standing in the room, which is enough to prove placement works.
    int poeCount = 0;
    for (const auto& m : floor.markers)
        if (m.type == 'P')
        {
            spawnBox(em, m.x, m.y, kPoeSize, 0.75f, 0.15f, 0.15f);
            ++poeCount;
        }

    const entt::entity playerEnt =
        spawnBox(em, floor.spawn_x, floor.spawn_y, kPlayerSize, 0.85f, 0.84f, 0.78f);
    em.registry().emplace<Camera>(playerEnt, Camera{floor.spawn_x, floor.spawn_y});
    player::bind(playerEnt);
    poe::log().info("boot: player at ({:.0f},{:.0f}), {} points of entry placed", floor.spawn_x,
                    floor.spawn_y, poeCount);

    engine.setCameraZoom(2.0f);
    engine.setGameUpdate(&player::update);
    engine.setRenderWorld(&gameRenderWorld);
    engine.setOnResize(&gameOnResize);
    engine.run();

    TileMapRenderer::shutdown();
    RenderSystem::shutdown();
    return 0;
}
