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
#include "CrashHandler.h"
#include "Engine.h"
#include "Formulas.h"
#include "SaveManager.h"
#include "Tunables.h"
#include "anim/AnimSetRegistry.h"
#include "anim/LocomotionConfig.h"
#include "anim/PoseSampler.h"
#include "anim/SkeletalAssets.h"
#include "audio/Audio.h"
#include "classmods/ClassModifiers.h"
#include "combat/AttackResolution.h"
#include "combat/CombatData.h"
#include "combat/CombatLog.h"
#include "combat/PlayerEquipment.h"
#include "dialog/GuideHandlers.h"
#include "dialog/TopicRegistry.h"
#include "gameplay/Actor.h"
#include "gameplay/AppearanceRegistry.h"
#include "gameplay/AppearanceTransformWriter.h"
#include "gameplay/BehaviorTree.h"
#include "gameplay/Enemies.h"
#include "gameplay/EnemyArchetype.h"
#include "gameplay/PerFrameTick.h"
#include "gameplay/PlayerState.h"
#include "gameplay/PropArchetype.h"
#include "gather/GatherSpawner.h"
#include "hair/HairRegistry.h"
#include "identity/Identity.h"
#include "insight/Insight.h"
#include "items/CategoryRegistry.h"
#include "items/HealHandlers.h"
#include "items/ItemRegistry.h"
#include "lang/Language.h"
#include "render/Camera.h"
#include "render/EquippedWeapon.h"
#include "render/HairRenderer.h"
#include "render/LightSpritePass.h"
#include "render/PickupMeshPass.h"
#include "render/PickupSpritePass.h"
#include "render/RegionGeometry.h"
#include "render/RegionShaders.h"
#include "render/ShadowPass.h"
#include "render/SkyPass.h"
#include "render/TerrainShader.h"
#include "render/TreeShader.h"
#include "softcaps/SoftCaps.h"
#include "spawn/FlowSpawner.h"
#include "ui/CharacterPreview.h"
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
#include <tracy/Tracy.hpp>

#include <SDL.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

namespace
{

// Tunables.json path — main.cpp loads at startup, the F1 panel saves
// back to the same path. Other paths (weapon classes, weapons, loadout)
// live in combat/CombatData.
const std::string kTunablesPath = "config/tunables.json";

void shutdownGeometry()
{
    // Production exit path skips this -- main calls std::_exit(0)
    // after the menu Quit, OS reclaims everything. Function kept as
    // public for any test/tool that opts out of the fast-exit path.
    selva::render::shutdownRegionGeometry();
    selva::render::shutdownRegionProgram();
    selva::render::shutdownSkyPass();
    selva::render::shutdownTreeShader();
    selva::render::shutdownTerrainShader();
    selva::render::shutdownShadowPass();
    selva::ui::shutdownCharacterPreview();
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
        // Unified resolution: the active character is always a
        // PlayerProfile in saveData, looked up by name. Unnamed runs
        // are name=="" entries; the equality loop resolves them the
        // same as named ones. Fallback default-profile only if the
        // lookup fails (defensive; shouldn't happen via normal flow).
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
        selva::gameplay::hardResetWorldForCharacter(*active);
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
    {
        ZoneScopedN("imgui-screens");
        if (selva::ui::renderScreens(engine))
            engine.requestQuit();
    }

    // F1 tuning panel is only relevant during Playing.
    if (selva::gameState().phase == selva::GameState::Phase::Playing)
    {
        ZoneScopedN("imgui-selva");
        selva::ui::selvaRenderImGui(engine, em);
    }
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

// Like runBootStep, but f() runs on a worker thread while the main
// thread pumps SDL events + redraws the loading screen at ~60Hz.
// Keeps the OS-level event loop alive so Windows doesn't mark the
// window unresponsive during a long synchronous CPU step.
//
// SAFETY: f() must touch ZERO main-thread-only state (OpenGL, ImGui,
// SDL window APIs, the engine physics shape registry concurrently
// with other writers). The current boot flow guarantees this because
// the only thing on the main thread during the loading screen is
// renderLoadingFrame itself, which is pure GL + ImGui and never
// touches Jolt. Adding new threaded steps requires the same audit.
template <typename F>
void runBootStepThreaded(::Engine& engine, const char* label, const char* loading_text, F&& f)
{
    const Uint64 t0 = SDL_GetTicks64();
    std::atomic<bool> done{false};
    std::thread worker(
        [&]
        {
            f();
            done.store(true, std::memory_order_release);
        });
    // Pump the loading screen until the worker finishes. ~16ms cadence
    // matches a 60Hz redraw and keeps Windows from flagging the window
    // unresponsive. renderLoadingFrame also pumps SDL events internally.
    while (!done.load(std::memory_order_acquire))
    {
        engine.renderLoadingFrame(loading_text);
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    worker.join();
    std::fprintf(stderr, "[boot] %s (threaded): %llums\n", label,
                 static_cast<unsigned long long>(SDL_GetTicks64() - t0));
}

} // namespace

namespace
{
void redirectStdioToLog()
{
    // WIN32-subsystem build has no console; reopen stdio onto
    // selva-oscura.log so existing fprintf diagnostics land somewhere
    // readable. Truncated per run.
    // freopen returns the new stream or nullptr on failure; we don't
    // care about the result -- if redirect fails, the existing stdout/
    // stderr stays connected to wherever it was (likely the console).
    (void)std::freopen("selva-oscura.log", "w", stdout);
    (void)std::freopen("selva-oscura.log", "a", stderr);
    // Line-buffered: progress as it happens without one-syscall-per-byte.
    std::setvbuf(stderr, nullptr, _IOLBF, 4096);
    std::setvbuf(stdout, nullptr, _IOLBF, 4096);
}

bool initRenderSubsystems()
{
    if (!selva::render::initRegionProgram())
    {
        std::fprintf(stderr, "Region shader compile/link failed\n");
        return false;
    }
    if (!selva::render::initSkyPass())
    {
        std::fprintf(stderr, "Sky pass shader compile/link failed\n");
        return false;
    }
    if (!selva::render::initTreeShader())
    {
        std::fprintf(stderr, "Tree shader compile/link failed\n");
        return false;
    }
    if (!selva::render::initTerrainShader())
    {
        std::fprintf(stderr, "Terrain shader compile/link failed\n");
        return false;
    }
    if (!selva::render::initShadowPass())
    {
        std::fprintf(stderr, "Shadow pass init failed\n");
        return false;
    }
    if (!selva::render::initLightSpritePass())
    {
        std::fprintf(stderr, "Light sprite pass init failed\n");
        return false;
    }
    if (!selva::render::initPickupSpritePass())
    {
        std::fprintf(stderr, "Pickup sprite pass init failed\n");
        return false;
    }
    if (!selva::ui::initCharacterPreview())
    {
        std::fprintf(stderr, "Character preview FBO init failed\n");
        return false;
    }
    return true;
}

void initRegionsAndTerrain(Engine& engine, engine::world::RegionId& out_default_region)
{
    runBootStep("initRegionGeometry",
                [&]
                {
                    selva::render::setInitialWindowSize(engine.windowWidth(),
                                                        engine.windowHeight());
                    selva::render::initRegionGeometry();
                });
    // RegionManager must exist before regions can register into it.
    engine::world::initRegionManager();
    // Post-commit hook: teleport the player capsule to the trigger's
    // declared spawn pos + yaw on each transition.
    engine::world::setPostCommitCallback(
        [](bool preserve_pos, const glm::vec3& spawn_pos, bool override_yaw, float spawn_yaw) {
            selva::gameplay::onRegionTransitionCommit(preserve_pos, spawn_pos, override_yaw,
                                                      spawn_yaw);
        });

    // Phase 1: parse region.json + register authored terrain modifiers
    // BEFORE initTerrain() (mesh builder queries the registry per
    // vertex). See [[feedback_dual_source_of_truth_is_the_bug]] -- the
    // chapel modifiers used to be C++-authored; they now live in
    // surface/region.json's terrain_modifiers array so the chapel
    // geometry has ONE source of truth.
    engine.renderLoadingFrame("regions");
    runBootStep("loadAllRegionsRegister",
                [&] { out_default_region = selva::world::loadAllRegionsRegister(); });
    runBootStep("registerAuthoredWorld",
                [] { selva::world::crypt_layout::registerAuthoredWorld(); });
    engine.renderLoadingFrame("terrain mesh");
    runBootStep("initTerrain", [] { selva::world::initTerrain(); });
    runBootStep("initHubRegion", [] { selva::world::initHubRegion(); });
    runBootStep("initTreeAssets", [] { selva::world::initTreeAssets(); });
    selva::world::initPhysicsRegion();
    // Build terrain physics shapes once, up front. Each JsonRegion
    // borrows handles from the global cache rather than rebuilding
    // its own copy (which used to triple the cost when 3 regions all
    // referenced the same terrain). Run on a worker thread so the
    // main thread can keep pumping SDL events + redrawing the
    // loading screen during the ~4-5s of MeshShape construction.
    runBootStepThreaded(engine, "buildAllTerrainShapes", "terrain physics",
                        [] { selva::world::buildAllTerrainShapes(); });

    // Phase 2: build per-region Jolt shapes for terrain + .glb meshes.
    engine.renderLoadingFrame("surface region");
    runBootStep("loadAllRegionsPreload", [] { selva::world::loadAllRegionsPreload(); });
    if (out_default_region != engine::world::kInvalidRegion)
    {
        runBootStep("activateRegionImmediate",
                    [&] { engine::world::activateRegionImmediate(out_default_region); });
    }
    else
    {
        std::fprintf(stderr, "[main] WARNING: no default spawn region; "
                             "scenes system inert, chapel will not render\n");
    }
}

void initGameplaySubsystems()
{
    // initPlayer needs the skeleton + mesh loaded so each actor's
    // sampler can bind.
    selva::gameplay::initPlayer();
    selva::gameplay::player().sampler.setFootIK(
        [](float x, float z) { return selva::world::sampleHeight(x, z); },
        /*position_enabled=*/false, /*orient_enabled=*/false);
    // Per-clip locomotion metadata BEFORE the audit so the audit can
    // print each clip's declared source.
    selva::anim::locomotionConfig().loadFromFile("config/locomotion.json");
    // Per-variant animation sets (humanoid_unarmed, humanoid_sword_and_shield,
    // etc.). Each set maps AnimIntent -> clip key; equipment-change code
    // swaps the active set pointer on actors. Load asserts every intent
    // is mapped so missing entries fail loud at startup.
    selva::anim::AnimSetRegistry::instance().loadFromDirectory("config/anim");
    // Appearance slider registry. Single source of truth for the
    // character-creator slider vocabulary; the creator UI iterates this
    // list to render sliders, so adding a new slider is one JSON edit.
    selva::gameplay::AppearanceRegistry::instance().loadFromFile("config/characters/sliders.json");
    selva::anim::auditClipHipMotion();
    // Inventory categories before items (item registry validates each
    // item's category against the category list).
    selva::items::categoryRegistry().loadFromFile("config/inventory_categories.json");
    selva::items::loadItemDirectory("config/items");
    selva::items::loadRecipeDirectory("config/recipes");
    // Pre-warm pickup mesh cache (same rationale as archetype-mesh
    // pre-warm below): first on-screen appearance of an item type
    // should not stall a gameplay frame with a synchronous disk read.
    selva::render::preloadAllPickupMeshes();
    // Weapon meshes (visual_weapon on ItemDef): same pattern -- first
    // equip should not stall. Trace analysis showed equipped-weapon
    // zone max 15.8ms on first equip prior to this pre-warm.
    selva::render::preloadAllEquippedWeaponMeshes();
    // Hair style registry + pre-warm every hair mesh at boot. First
    // hair-choice change in the character creator (or first appearance
    // of an NPC with a hair_style_id) should not stall a frame with a
    // cold mesh load. 56 styles at ~3MB each = ~180MB resident but
    // fully paid-once at boot; runtime hair swap is a cache hit.
    selva::hair::loadHairRegistry("config/hair_styles.json");
    selva::render::preloadAllHairMeshes();
    // Dialog handlers BEFORE the topic registry so JSON-referenced
    // handler keys resolve at first-use.
    selva::dialog::registerGuideHandlers();
    selva::dialog::topicRegistry().loadDirectory("config/npcs");
    // Archetypes BEFORE region activation -- spawn looks them up by id
    // as JsonRegion::commitPrepared() iterates enemy_spawns + props.
    selva::gameplay::archetypes().loadDirectory("config/enemies");
    selva::gameplay::archetypes().resolveAllActionReach();
    // Pre-warm archetype meshes so the first spawn of each variant
    // doesn't stall a gameplay frame with a synchronous disk read +
    // GPU upload. Walk every registered archetype, force-load both
    // mesh_path (single-variant archetypes) and every mesh_path_variants
    // entry (multi-variant archetypes like larvae) through the
    // per-path cache in SkeletalAssets. Load failures are silent-
    // benign: the archetype's spawn will hit the same failure path
    // later and fall back to the shared bundle mesh.
    //
    // Root cause this addresses: prior to pre-warm, the acheron_larvae
    // spawn-flow's initial-fill would cold-load 1-2 mesh variants on
    // the first frame after region activation (Continue-button click),
    // stalling the main thread ~290ms. Trace 11 captured this as a
    // tickFlowSpawner spike at t=18s.
    for (const auto& [id, arch] : selva::gameplay::archetypes().all())
    {
        if (!arch.mesh_path.empty())
            selva::anim::meshByArchetypePath(arch.mesh_path, arch.skeleton_id);
        for (const auto& variant : arch.mesh_path_variants)
            selva::anim::meshByArchetypePath(variant, arch.skeleton_id);
    }
    selva::gameplay::propArchetypes().loadDirectory("config/props");
    // Built-in AppearanceTransform sources -- registers
    // `archetype_transform` (the generalized replacement for the
    // legacy EnemyArchetype.transform_target_archetype lerp path).
    // Must run AFTER archetypes() loads so the source's
    // per-actor target_path lookup can resolve archetype refs.
    selva::gameplay::initBuiltinAppearanceTransformSources();
    // FlowSpawner AFTER archetypes() (each flow validates its archetype
    // reference at load time).
    selva::spawn::initFlowSpawner();
    selva::gather::initGatherSpawner();
    selva::items::registerHealHandlers();
    selva::gameplay::initBehaviorTrees();
    // Region bodies + meshes were registered at boot, but enemy
    // spawning needs archetypes + trees loaded first.
    selva::world::spawnAllRegionEnemies();
    // Prop spawn needs PropArchetypeRegistry loaded (above) + the
    // TreeAssets variant list populated (initHubRegion-time) so the
    // variant-name reverse lookup resolves. Cylinders appended here
    // join the C++-literal cylinders from initHubRegion + the
    // procgen scatter from populateHubTrees in the same render pass.
    selva::world::spawnAllRegionProps();
}

void initCombatData()
{
    int n_classes = 0;
    int n_weapons = 0;
    selva::combat::loadAllCombatData(&n_classes, &n_weapons);
    const auto& eq = selva::combat::equipment();
    selva::combat::resolveAttackCancelOpenTimes(
        selva::combat::weaponClasses(), selva::anim::clips(), selva::gameplay::player().sampler);
    std::fprintf(stderr, "[combat] loaded %d class(es), %d weapon(s); right=%s left=%s grip=%s\n",
                 n_classes, n_weapons, eq.right ? eq.right->id.c_str() : "(empty)",
                 eq.left ? eq.left->id.c_str() : "(empty)",
                 eq.grip == selva::combat::Grip::TwoHanded ? "two_handed" : "one_handed");
}
} // namespace

int main(int /*argc*/, char* /*argv*/[])
{
    redirectStdioToLog();
    // Combat debug + sampler diagnostics flow through the engine::log
    // channel registered as "combat" -- opened lazily by the F1 toggle.

    // Build fingerprint -- first line of every run's log. Lets us
    // answer "which exact binary did this run use?" without having
    // to stat the .exe after the fact (often by then the binary
    // has been rebuilt for the next attempt). Stale-binary
    // misattribution has bitten investigations before; this kills
    // the ambiguity at the source.
#ifndef SELVA_GIT_COMMIT
#define SELVA_GIT_COMMIT "unknown"
#endif
#ifndef SELVA_CONFIGURE_TIME
#define SELVA_CONFIGURE_TIME "unknown"
#endif
    std::fprintf(stderr, "[build] selva-oscura commit=%s configured=%s\n", SELVA_GIT_COMMIT,
                 SELVA_CONFIGURE_TIME);
    std::fflush(stderr);

    // Install the crash handler IMMEDIATELY -- before any subsystem
    // init, so a crash during init also gets a dump. Callbacks left
    // null at this point; we'll populate them after SaveManager and
    // the ui/Screens setPhase chokepoint are wired up. Default
    // crash dir is "./crashes" until SDL gives us the real pref
    // path (then setCrashSaveDirectory below switches).
    engine::installCrashHandler({}, {});

    Engine engine;
    engine.setMSAA(8);
    engine.setWindowMode(Engine::WindowMode::BorderlessFullscreen);
    if (!engine.init("Selva Oscura", 1280, 720))
    {
        std::fprintf(stderr, "Engine init failed\n");
        return 1;
    }
    // Loading screen: first frame as soon as the window + GL context
    // exist so the user sees something instead of a black "(Not
    // Responding)" window during the multi-second init below.
    engine.renderLoadingFrame("starting up");
    // Linear-space; originally sRGB-authored (0.16, 0.18, 0.22) -- the
    // sky/atmosphere baseline. The "keep in sync with kFogCool" comment
    // that used to live here was wrong: the original value matches
    // surface/region.json's ambient.color, NOT kFogCool. With the sRGB
    // framebuffer doing the encode on write, this needs to be linear
    // for the displayed bytes to match the original eyeballed value.
    engine.setClearColor(0.0220f, 0.0272f, 0.0397f);
    // Cursor capture is managed by ui/Screens.cpp's state machine;
    // we boot in MainMenu so leave the cursor free.
    SDL_SetRelativeMouseMode(SDL_FALSE);
    if (!initRenderSubsystems())
        return 1;
    // Language map FIRST -- region activation registers examine
    // interactables that resolve through lang::resolve() during
    // initRegionsAndTerrain. If lang loads after regions, those
    // interactables snapshot [lang:KEY] missing-key fallbacks as
    // their permanent labels.
    runBootStep("lang::loadDirectory", [] { selva::lang::loadDirectory("config/lang"); });
    // Insight graph loads its node definitions from config/insight.
    // Cheap; nodes are just trigger-decl structs. Eval happens per
    // frame via insight::tick().
    runBootStep("insight::loadDirectory", [] { selva::insight::loadDirectory("config/insight"); });
    engine.renderLoadingFrame("region geometry");
    engine::world::RegionId default_region;
    initRegionsAndTerrain(engine, default_region);

    // Tunables + formulas BEFORE actor pool init so computeMaxHp /
    // Stamina / Poise read the JSON-tuned coefficients. Each falls
    // back silently to struct defaults if its file is missing.
    selva::tuning::loadFromFile(kTunablesPath);
    selva::formulas::loadFromFile("config/balance/formulas.json");
    // Per-class identity-stat functions per
    // [[project_identity_stats_derived_erasure_locked_2026_06_14]].
    // Failure is silent (Identity::loadFromFile logs); compute()
    // returns 0 for any class whose function didn't load.
    selva::identity::loadFromFile("config/balance/identity_functions.json");
    // Per-class soft-cap curves on derived stat values per
    // [[project_class_stats_v2_locked_2026_06_14]]. Failure is silent
    // (SoftCaps::loadFromFile logs); apply() returns the raw value
    // unchanged for any class/derived pair whose curve didn't load.
    selva::softcaps::loadFromFile("config/balance/soft_caps.json");
    // Per-class flat modifiers (hp_offset etc) layering on top of
    // soft-cap-shaped contributions. The Penitent's body carries
    // more vital substance than the Heretic's; their class adds a
    // flat HP value separate from END investment. Per the same
    // doctrine. Failure is silent; offsetFor returns 0.
    selva::classmods::loadFromFile("config/balance/class_modifiers.json");
    engine.renderLoadingFrame("audio + animations");
    runBootStep("audio::init", [] { selva::audio::init("config/audio.json"); });
    bool skel_ok = false;
    runBootStep("initSkeletalAssets", [&] { skel_ok = selva::anim::initSkeletalAssets(); });
    if (!skel_ok)
        std::fprintf(stderr, "[main] skeletal assets failed to load -- character disabled\n");
    else
        runBootStep("initGameplaySubsystems", [] { initGameplaySubsystems(); });
    runBootStep("initCombatData", [] { initCombatData(); });
    // Persisted SaveData -- defaults if missing. Settings apply at load.
    selva::saveData() = selva::SaveManager::load();

    // Crash handler -- now that SaveManager + setPhase exist, wire
    // their callbacks. Switch the crash dump directory to the same
    // per-user pref path the save file lives in (resolved by SDL --
    // safe to call now that engine.init() ran).
    engine::setCrashSaveDirectory(selva::SaveManager::getSaveDir() + "crashes");
    engine::setCrashRecoveryCallbacks(
        /*save_cb=*/
        []() -> bool
        {
            // Emergency save: snapshot the active runtime state back
            // into the active profile, then write the SaveData blob.
            // Each step is best-effort; we'd rather have a partial
            // save than no save.
            selva::PlayerProfile* active = selva::activePlayerProfile();
            if (active != nullptr)
                selva::gameplay::saveActiveCharacterFromPlayer(*active);
            return selva::SaveManager::save(selva::saveData());
        },
        /*teardown_cb=*/
        []()
        {
            // Force-return to MainMenu. setPhase is the existing
            // chokepoint that tears down audio, region, actors etc.
            // before swapping phase; using it (rather than a custom
            // teardown path) keeps the recovery flow on the same
            // rails as a normal main-menu return.
            selva::ui::setPhase(selva::GameState::Phase::MainMenu);
        });

    engine.setPerFrameUpdate(&gatedPerFrame);
    engine.setRenderWorld(&gatedRenderWorld);
    engine.setRenderImGui(&gatedRenderImGui);
    engine.setOnResize(&selva::render::onWindowResize);
    // Pre-warm screen assets (main menu logo, etc.) so the first render
    // of MainMenu doesn't stall with a synchronous texture load. Trace
    // 20 identified `screens-mainmenu` first-frame spikes to 31 ms from
    // this exact path.
    selva::ui::preloadScreenAssets();
    // scripts/smoke_test.sh greps this exact string to confirm the game
    // reached the main loop without a pre-main crash. Don't reword
    // without updating the script.
    std::fprintf(stderr, "[smoke] main loop ready\n");
    std::fflush(stderr);
    engine.run();
    // Shutdown sequence:
    //   1. Hide the window IMMEDIATELY so the player sees a clean
    //      disappear instead of a frozen frame during teardown.
    //   2. Flush the save (defensive; the in-game Save & Quit path
    //      already saved, but this catches any "user closed via
    //      window X" path that bypasses the menu).
    //   3. std::_Exit(0) -- skip C++ destructors entirely. The OS
    //      reclaims all memory, file handles, GL context, audio
    //      device, threads. This is the standard shipping-game
    //      pattern (Doom/Source/Unity all do this) and is the
    //      ONLY way to avoid the slow-destructor + threaded-audio
    //      hang we hit under ASan when freeing the multi-megabyte
    //      terrain vectors. Regular Debug builds also benefit:
    //      instant exit, zero possibility of deadlock at the
    //      audio/GL/Jolt teardown boundary.
    //
    // Anything that needs to PERSIST across runs must already have
    // been flushed to disk before this point. Per `flushAndSave`
    // (Screens.cpp), every Save & Quit path covers this.
    engine.hideWindow();
    std::_Exit(0);
    // engine::log channels close their files at process shutdown via
    // Channel::~Channel; nothing to clean up here.
    return 0;
}
