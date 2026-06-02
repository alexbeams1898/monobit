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
#include "Formulas.h"
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
#include "items/CategoryRegistry.h"
#include "items/ItemRegistry.h"
#include "items/SealHandlers.h"
#include "render/Camera.h"
#include "render/LightSpritePass.h"
#include "render/RegionGeometry.h"
#include "render/RegionShaders.h"
#include "render/ShadowPass.h"
#include "render/SkyPass.h"
#include "render/TerrainShader.h"
#include "render/TreeShader.h"
#include "ui/Screens.h"
#include "ui/TuningPanel.h"
#include "world/Collision.h"
#include "world/CryptLayout.h"
#include "world/PhysicsRegion.h"
#include "world/Region.h"
#include "world/RegionBootstrap.h"
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
    selva::render::shutdownRegionGeometry();
    selva::render::shutdownRegionProgram();
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
        const selva::PlayerProfile default_profile;
        const selva::PlayerProfile* active = &default_profile;
        for (const auto& c : selva::saveData().characters)
        {
            if (c.name == gs.active_character)
            {
                active = &c;
                break;
            }
        }
        selva::gameplay::loadActiveCharacterIntoWorld(*active);
        gs.pending_world_create = false;
        gs.world_initialized = true;
        // If this was a New Game, queue the wake-up Scene. Load-Game
        // paths don't set pending_wake_scene -- the loaded character is
        // wherever they were saved and shouldn't play the wake again.
        if (gs.pending_wake_scene)
        {
            selva::gameplay::beginWakeScene();
            gs.pending_wake_scene = false;
        }
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

// Time + log one boot-init step. Pattern is identical at every
// boot site (capture t0, run f, log elapsed ms); the helper keeps
// main() under the readability-function-size threshold without
// hiding what each step does.
template <typename F> void runBootStep(const char* label, F&& f)
{
    const Uint64 t0 = SDL_GetTicks64();
    f();
    std::fprintf(stderr, "[boot] %s: %llums\n", label,
                 static_cast<unsigned long long>(SDL_GetTicks64() - t0));
}

} // namespace

int main(int /*argc*/, char* /*argv*/[])
{
    // WIN32-subsystem build has no console; reopen stdout/stderr onto
    // selva-oscura.log next to the exe so existing fprintf(stderr)
    // diagnostics still land somewhere readable. Truncate per run so
    // the log reflects the latest session only.
    std::freopen("selva-oscura.log", "w", stdout);
    std::freopen("selva-oscura.log", "a", stderr);
    // Line-buffered: get progress as it happens without one-syscall-per-byte.
    std::setvbuf(stderr, nullptr, _IOLBF, 4096);
    std::setvbuf(stdout, nullptr, _IOLBF, 4096);

    // Combat debug + sampler diagnostics flow through the
    // engine::log::Channel registered under "combat" — opened lazily
    // by the F1 toggle (selva::combat::setCombatDebugEnabled). No
    // startup work needed here; the channel system handles file
    // open + sampler routing through one named lookup.

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

    if (!selva::render::initRegionProgram())
    {
        std::fprintf(stderr, "Region shader compile/link failed\n");
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
    if (!selva::render::initLightSpritePass())
    {
        std::fprintf(stderr, "Light sprite pass init failed\n");
        return 1;
    }

    engine.renderLoadingFrame("region geometry");

    runBootStep("initRegionGeometry",
                [&]
                {
                    selva::render::setInitialWindowSize(engine.windowWidth(),
                                                        engine.windowHeight());
                    selva::render::initRegionGeometry();
                });
    // RegionManager must exist before regions can register into it.
    engine::world::initRegionManager();
    // Post-commit hook: on each transition, after the new region is
    // committed, teleport the player capsule to the trigger's
    // declared spawn pos + yaw. Engine doesn't know about the
    // player; game wires it.
    engine::world::setPostCommitCallback(
        [](bool preserve_pos, const glm::vec3& spawn_pos, bool override_yaw, float spawn_yaw) {
            selva::gameplay::onRegionTransitionCommit(preserve_pos, spawn_pos, override_yaw,
                                                     spawn_yaw);
        });

    // PHASE 1 of region loading: parse region.json files + register
    // their authored terrain modifiers into the global modifier
    // registry. Modifiers MUST be registered before initTerrain()
    // because Terrain.cpp::buildRegionMesh queries the registry per
    // vertex when building the mesh. See [[feedback_dual_source_of_truth_is_the_bug]]
    // -- chapel terrain modifiers used to be authored in C++; they
    // now live in surface/region.json's terrain_modifiers array so
    // chapel geometry has ONE source of truth (the region JSON).
    engine.renderLoadingFrame("regions");
    engine::world::RegionId default_region;
    runBootStep("loadAllRegionsRegister",
                [&] { default_region = selva::world::loadAllRegionsRegister(); });

    // registerLimboLights is the last surviving C++-side authored
    // world content. Lights are not terrain modifiers and don't have
    // a JSON schema yet; future commit can move them into JSON too.
    runBootStep("registerAuthoredWorld",
                [] { selva::world::crypt_layout::registerAuthoredWorld(); });
    engine.renderLoadingFrame("terrain mesh");
    runBootStep("initTerrain", [] { selva::world::initTerrain(); });
    runBootStep("initHubRegion", [] { selva::world::initHubRegion(); });
    runBootStep("initTreeAssets", [] { selva::world::initTreeAssets(); });
    // Physics: register terrain as static trimesh bodies. Chapel +
    // descent + limbo geometry is loaded by JsonRegion::preloadAssets
    // (per-region static_meshes array) and registered into Jolt at
    // region activation, not here.
    selva::world::initPhysicsRegion();

    // PHASE 2 of region loading: build per-region Jolt shapes for
    // terrain + .glb meshes. Needs initTerrain() to have completed
    // so the terrain CPU vertex arrays exist.
    engine.renderLoadingFrame("surface region");
    runBootStep("loadAllRegionsPreload", [] { selva::world::loadAllRegionsPreload(); });

    if (default_region != engine::world::kInvalidRegion)
    {
        runBootStep("activateRegionImmediate",
                    [&] { engine::world::activateRegionImmediate(default_region); });
    }
    else
    {
        std::fprintf(stderr, "[main] WARNING: no default spawn region; "
                             "scenes system inert, chapel will not render\n");
    }

    // Load runtime-tunable values + RPG formula constants BEFORE
    // initializing actor pools so computeMaxHp/Stamina/Poise reads the
    // JSON-tuned coefficients. Each falls back silently to struct
    // defaults if its file is missing or malformed.
    selva::tuning::loadFromFile(kTunablesPath);
    selva::formulas::loadFromFile("config/balance/formulas.json");

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

        // Inventory category registry first; item registry validates
        // each item's category against it so this MUST load before items.
        // Use-handler registrations BEFORE the item registry so JSON
        // references to handler keys resolve cleanly when items load.
        selva::items::categoryRegistry().loadFromFile("config/inventory_categories.json");
        selva::items::registerSealHandlers();
        selva::items::itemRegistry().loadDirectory("config/items");

        // Load enemy archetypes (action lists, perception overrides).
        // Must run before any region activation -- spawn looks up
        // archetype by id from this registry as JsonRegion::
        // commitPrepared() iterates enemy_spawns.
        selva::gameplay::archetypes().loadDirectory("config/enemies");

        // Construct + register the behavior trees that archetypes
        // bind to via tree_id. Must run before any region activation
        // spawns enemies since decision ticks look up the tree on
        // first fire.
        selva::gameplay::initBehaviorTrees();

        // Region bodies + meshes were registered at boot (line above),
        // but enemy spawning needs gameplay (archetypes + trees)
        // loaded first. Now that gameplay is ready, spawn enemies for
        // EVERY resident region so the player encounters them in
        // whichever region they cross into. Multi-resident: same
        // doctrine as the renderer + physics.
        selva::world::spawnAllRegionEnemies();
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

    // Smoke-test marker: scripts/smoke_test.sh greps for this exact
    // string to confirm the game reached the main loop without a
    // pre-main crash (static-init-order fiasco, missing asset, etc).
    // Don't change the wording without updating the script.
    std::fprintf(stderr, "[smoke] main loop ready\n");
    std::fflush(stderr);

    engine.run();

    selva::gameplay::shutdownHubEnemies();
    selva::anim::shutdownSkeletalAssets();
    engine::world::shutdownRegionManager();
    shutdownGeometry();
    selva::audio::shutdown();
    engine.shutdown();
    // engine::log channels are owned by the static registry — they
    // close their files at process shutdown via Channel::~Channel.
    return 0;
}
