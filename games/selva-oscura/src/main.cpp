// stb_image_write — single-header PNG writer for the F1-debug frame
// capture feature. STB_IMAGE_WRITE_IMPLEMENTATION must live in exactly
// one .cpp file across the binary; the per-frame tick is the only
// consumer (frame capture readback runs in selvaRenderWorld, which now
// lives in gameplay/PerFrameTick.cpp). The define stays in main.cpp
// because main.cpp is the entry point's TU and the build expects to
// find stbi_write_png symbols here.
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "AppState.h"
#include "AppStateGlobal.h"
#include "Engine.h"
#include "SaveManager.h"
#include "Tunables.h"
#include "anim/LocomotionConfig.h"
#include "anim/PoseSampler.h"
#include "anim/SkeletalAssets.h"
#include "audio/Audio.h"
#include "combat/AttackResolution.h"
#include "combat/CombatData.h"
#include "combat/CombatLog.h"
#include "combat/PlayerEquipment.h"
#include "gameplay/Actor.h"
#include "gameplay/BehaviorTree.h"
#include "gameplay/Enemies.h"
#include "gameplay/EnemyArchetype.h"
#include "gameplay/PerFrameTick.h"
#include "gameplay/PlayerState.h"
#include "render/Camera.h"
#include "render/SceneGeometry.h"
#include "render/SceneShaders.h"
#include "render/ShadowPass.h"
#include "render/SkyPass.h"
#include "render/TerrainShader.h"
#include "render/TreeShader.h"
#include "ui/Screens.h"
#include "ui/TuningPanel.h"
#include "world/Collision.h"
#include "world/CryptLayout.h"
#include "world/PhysicsScene.h"
#include "world/Scene.h"
#include "world/SceneBootstrap.h"
#include "world/StaticMeshAssets.h"
#include "world/Terrain.h"
#include "world/TreeAssets.h"

#include <stb_image_write.h>

#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <cstdio>
#include <string>

namespace
{

// Tunables.json path — main.cpp loads at startup, the F1 panel saves
// back to the same path. Other paths (weapon classes, weapons, loadout)
// live in combat/CombatData.
const std::string kTunablesPath = "config/tunables.json";

void shutdownGeometry()
{
    selva::render::shutdownSceneGeometry();
    selva::render::shutdownSceneProgram();
    selva::render::shutdownSkyPass();
    selva::render::shutdownTreeShader();
    selva::render::shutdownTerrainShader();
    selva::render::shutdownShadowPass();
    selva::world::shutdownTerrain();
    selva::world::shutdownTreeAssets();
}

// Phase-gated per-frame update: only ticks game state when Playing and the
// pause menu is closed. Keeps the engine's loop running every frame so the
// ImGui pass continues to handle menus.
//
// When skipping, we still sync the mouse-button prev-state so edges don't
// fire on resume (otherwise RMB held to close the menu would trigger a
// fresh block-press the next frame).
//
// On the first frame we enter Playing from a menu (CharCreate or LoadGame
// set pending_world_create=true), reset the player to the spawn point.
// Without this, picking "New Game" or "Load Game" after a previous run
// would carry the previous run's pos / hp / velocity into the new one.
void gatedPerFrame(::Engine& engine, ::EntityManager& em, double dt)
{
    auto& gs = selva::gameState();
    const bool playing = (gs.phase == selva::GameState::Phase::Playing);
    const bool menu_open = selva::uiState().isScreenOpen();
    if (!playing || menu_open)
    {
        selva::gameplay::syncInputEdgesFromCurrentState();
        return;
    }
    if (gs.pending_world_create)
    {
        // Find the active character's profile in SaveData and load it.
        // If the active name isn't in the save (shouldn't happen via
        // normal flow; defensive), pass a default-constructed profile -
        // the load still produces a clean spawn, just with no name-tied
        // persistent state.
        selva::PlayerProfile default_profile;
        const selva::PlayerProfile* active = &default_profile;
        for (const auto& c : selva::saveData().characters)
        {
            if (c.name == gs.active_character)
            {
                active = &c;
                break;
            }
        }
        selva::gameplay::loadActiveCharacterIntoPlayer(*active);
        selva::gameplay::resetEnemiesToSpawn();
        gs.pending_world_create = false;
        gs.world_initialized = true;
    }
    selva::gameplay::selvaPerFrame(engine, em, dt);
}

// Phase-gated world render: skipped when not Playing so the menu draws
// against the engine's clear color rather than a partially-rendered world.
void gatedRenderWorld(::Engine& engine, ::EntityManager& em, float camX, float camY, float alpha)
{
    const auto& gs = selva::gameState();
    if (gs.phase != selva::GameState::Phase::Playing)
        return;
    selva::gameplay::selvaRenderWorld(engine, em, camX, camY, alpha);
}

// ImGui render hook. Draws screens (main menu, char-create, load, settings,
// pause overlay) first; then the F1 tuning panel during Playing. requestQuit
// is invoked when the user picks Quit from main menu or pause menu.
void gatedRenderImGui(::Engine& engine, ::EntityManager& em)
{
    if (selva::ui::renderScreens(engine))
        engine.requestQuit();

    // F1 tuning panel is only relevant during Playing.
    if (selva::gameState().phase == selva::GameState::Phase::Playing)
        selva::ui::selvaRenderImGui(engine, em);
}

} // namespace

int main(int /*argc*/, char* /*argv*/[])
{
    // Combat-debug log file: opened at startup when combat-debug is on
    // by default; otherwise lazily by the F1 toggle. Truncate on open
    // so each run starts with a fresh log.
    if (selva::combat::isCombatDebugEnabled())
    {
        FILE* log = selva::combat::openCombatLog();
        if (log != nullptr)
            std::fprintf(stderr, "[combat:log] writing diagnostics to combat-debug.log\n");
        // Route sampler-side loco diagnostics into the same file.
        selva::anim::setSamplerDiagLog(log);
    }

    Engine engine;

    // 4x MSAA — smooths cube/floor edge silhouettes so they don't crawl
    // when the camera rotates.
    engine.setMSAA(8);

    // Maximized window with title bar/resize handles. 1280x720 is the
    // restore size when un-maximized.
    engine.setWindowMode(Engine::WindowMode::BorderlessFullscreen);

    if (!engine.init("Selva Oscura", 1280, 720))
    {
        std::fprintf(stderr, "Engine init failed\n");
        return 1;
    }

    // Loading screen: first frame as soon as the window + GL context
    // exist so the user sees something other than a black
    // "(Not Responding)" window during the multi-second mesh + physics
    // init below. Each renderLoadingFrame call appends the previous
    // step to the completed list and shows the new current step.
    engine.renderLoadingFrame("starting up");

    // Keep in sync with kFogCool in PerFrameTick.cpp.
    engine.setClearColor(0.16f, 0.18f, 0.22f);

    // Cursor capture is managed by the screen state machine (see
    // ui/Screens.cpp tickMouseCapture). At startup we are in MainMenu, so
    // leave the cursor free until the player enters Playing.
    SDL_SetRelativeMouseMode(SDL_FALSE);

    if (!selva::render::initSceneProgram())
    {
        std::fprintf(stderr, "Scene shader compile/link failed\n");
        return 1;
    }
    if (!selva::render::initSkyPass())
    {
        std::fprintf(stderr, "Sky pass shader compile/link failed\n");
        return 1;
    }
    if (!selva::render::initTreeShader())
    {
        std::fprintf(stderr, "Tree shader compile/link failed\n");
        return 1;
    }
    if (!selva::render::initTerrainShader())
    {
        std::fprintf(stderr, "Terrain shader compile/link failed\n");
        return 1;
    }
    if (!selva::render::initShadowPass())
    {
        std::fprintf(stderr, "Shadow pass init failed\n");
        return 1;
    }

    engine.renderLoadingFrame("scene geometry");

    selva::render::setInitialWindowSize(engine.windowWidth(), engine.windowHeight());
    selva::render::initSceneGeometry();
    // Static mesh assets MUST load before chapel terrain modifiers so
    // the modifier registration can auto-derive the chapel footprint
    // from the loaded mesh's XZ AABB (no hand-set coords).
    selva::world::initStaticMeshAssets();
    // Terrain modifiers (chapel plateau, descent strip, etc) must be
    // registered BEFORE initTerrain() — Terrain.cpp::buildRegionMesh
    // queries the registry per vertex. Modifiers + heightmap PNG
    // together produce the final terrain Y; render mesh and physics
    // trimesh share that same Y.
    selva::world::crypt_layout::registerChapelTerrainModifiers();
    engine.renderLoadingFrame("terrain mesh");
    selva::world::initTerrain();
    selva::world::initHubScene();
    selva::world::initTreeAssets();
    // (initStaticMeshAssets moved earlier — chapel footprint derived from mesh)
    // Physics: register terrain + chapel as static trimesh bodies.
    // MUST run after both terrain and static-mesh-assets init so the
    // CPU vertex copies exist on those structs.
    selva::world::initPhysicsScene();

    // Scene manager bootstrap + initial activation. The default
    // spawn scene becomes the active scene at boot; its onActivate
    // registers chapel/static-mesh bodies in Jolt. Legacy paths
    // above have stopped registering the chapel (initPhysicsScene
    // no longer calls registerChapel) so there's no double-register.
    //
    // Terrain modifiers (chapel plateau, shaft hole) are still
    // registered BEFORE initTerrain via the call above, because
    // terrain is a global singleton whose mesh is built once at
    // initTerrain time. When per-scene terrain ships, those
    // modifier declarations move into the scene's scene.json.
    engine::world::initSceneManager();
    // Post-commit hook: on each transition, after the new scene is
    // committed, teleport the player capsule to the trigger's
    // declared spawn pos + yaw. Engine doesn't know about the
    // player; game wires it.
    engine::world::setPostCommitCallback(
        [](bool preserve_pos, const glm::vec3& spawn_pos, bool override_yaw, float spawn_yaw) {
            selva::gameplay::onSceneTransitionCommit(preserve_pos, spawn_pos, override_yaw,
                                                     spawn_yaw);
        });
    engine.renderLoadingFrame("surface scene");
    const engine::world::SceneId default_scene = selva::world::loadAllScenes();
    if (default_scene != engine::world::kInvalidScene)
    {
        engine::world::activateSceneImmediate(default_scene);
    }
    else
    {
        std::fprintf(stderr, "[main] WARNING: no default spawn scene; "
                             "scenes system inert, chapel will not render\n");
    }

    // Load runtime-tunable values BEFORE initializing actor pools so
    // their derived HP / stamina maxima read the JSON-tuned
    // coefficients (hp_per_vig, stamina_per_end). Falls back silently
    // to struct defaults if the file is missing or malformed.
    selva::tuning::loadFromFile(kTunablesPath);

    engine.renderLoadingFrame("audio + animations");
    // Audio: init miniaudio engine + load name→path registry. Safe to
    // run before/after asset load; playSfx no-ops if init failed (no
    // audio hardware) or the name isn't registered.
    selva::audio::init("config/audio.json");

    if (!selva::anim::initSkeletalAssets())
    {
        std::fprintf(stderr, "[main] skeletal assets failed to load — character disabled\n");
    }
    else
    {
        // Player + enemy actor pool needs the skeleton + mesh
        // loaded so each actor's sampler can bind. initPlayer must
        // run after initSkeletalAssets.
        selva::gameplay::initPlayer();

        selva::gameplay::player().sampler.setFootIK(
            [](float x, float z) { return selva::world::sampleHeight(x, z); },
            /*position_enabled=*/false, /*orient_enabled=*/false);

        // Per-clip locomotion metadata: blend-in durations,
        // translation_source declarations. Loaded before the audit
        // so the audit can print each clip's declared source.
        selva::anim::locomotionConfig().loadFromFile("config/locomotion.json");

        // Dump per-clip hip path + authored speed + translation
        // source. Catches "I added a clip but didn't declare a
        // source" at startup.
        selva::anim::auditClipHipMotion();

        // Load enemy archetypes (action lists, perception overrides).
        // Must run before initHubEnemies — spawn looks up archetype
        // by id from this registry.
        selva::gameplay::archetypes().loadDirectory("config/enemies");

        // Construct + register the behavior trees that archetypes
        // bind to via tree_id. Must run before initHubEnemies since
        // decision ticks lookup the tree at first fire.
        selva::gameplay::initBehaviorTrees();

        selva::gameplay::initHubEnemies();
    }

    // Combat data: weapon classes, weapons, equipment loaded via
    // combat/CombatData. Synthesizes "fists" for empty hand slots.
    // Resolves cancel-open / chain-link-start times after load.
    {
        int n_classes = 0;
        int n_weapons = 0;
        selva::combat::loadAllCombatData(&n_classes, &n_weapons);
        const auto& eq = selva::combat::equipment();
        selva::combat::resolveAttackCancelOpenTimes(selva::combat::weaponClasses(),
                                                    selva::anim::clips(),
                                                    selva::gameplay::player().sampler);
        std::fprintf(stderr,
                     "[combat] loaded %d class(es), %d weapon(s); right=%s left=%s grip=%s\n",
                     n_classes, n_weapons, eq.right ? eq.right->id.c_str() : "(empty)",
                     eq.left ? eq.left->id.c_str() : "(empty)",
                     eq.grip == selva::combat::Grip::TwoHanded ? "two_handed" : "one_handed");
    }

    // Load persisted SaveData. Returns defaults (empty character list) if
    // no save exists yet. Settings (audio volumes, etc.) apply immediately.
    selva::saveData() = selva::SaveManager::load();

    engine.setPerFrameUpdate(&gatedPerFrame);
    engine.setRenderWorld(&gatedRenderWorld);
    engine.setRenderImGui(&gatedRenderImGui);
    engine.setOnResize(&selva::render::onWindowResize);

    engine.run();

    selva::gameplay::shutdownHubEnemies();
    selva::anim::shutdownSkeletalAssets();
    engine::world::shutdownSceneManager();
    shutdownGeometry();
    selva::audio::shutdown();
    engine.shutdown();
    selva::combat::closeCombatLog();
    return 0;
}
