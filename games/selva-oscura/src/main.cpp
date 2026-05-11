// stb_image_write — single-header PNG writer for the F1-debug frame
// capture feature. STB_IMAGE_WRITE_IMPLEMENTATION must live in exactly
// one .cpp file across the binary; the per-frame tick is the only
// consumer (frame capture readback runs in selvaRenderWorld, which now
// lives in gameplay/PerFrameTick.cpp). The define stays in main.cpp
// because main.cpp is the entry point's TU and the build expects to
// find stbi_write_png symbols here.
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "Engine.h"
#include "Tunables.h"
#include "anim/LocomotionConfig.h"
#include "anim/PoseSampler.h"
#include "anim/SkeletalAssets.h"
#include "combat/AttackResolution.h"
#include "combat/CombatData.h"
#include "combat/CombatLog.h"
#include "combat/PlayerEquipment.h"
#include "gameplay/Enemies.h"
#include "gameplay/PerFrameTick.h"
#include "gameplay/Actor.h"
#include "gameplay/PlayerState.h"
#include "render/Camera.h"
#include "render/SceneGeometry.h"
#include "render/SceneShaders.h"
#include "ui/TuningPanel.h"
#include "world/Collision.h"

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
    engine.setMSAA(4);

    // Maximized window with title bar/resize handles. 1280x720 is the
    // restore size when un-maximized.
    engine.setWindowMode(Engine::WindowMode::Maximized);

    if (!engine.init("Selva Oscura", 1280, 720))
    {
        std::fprintf(stderr, "Engine init failed\n");
        return 1;
    }

    engine.setClearColor(0.0f, 0.0f, 0.0f);

    // Capture the cursor for mouse-look.
    SDL_SetRelativeMouseMode(SDL_TRUE);
    SDL_GetRelativeMouseState(nullptr, nullptr);

    if (!selva::render::initSceneProgram())
    {
        std::fprintf(stderr, "Scene shader compile/link failed\n");
        return 1;
    }

    selva::render::setInitialWindowSize(engine.windowWidth(), engine.windowHeight());
    selva::render::initSceneGeometry();
    selva::world::initHubScene();

    // Load runtime-tunable values BEFORE initializing actor pools so
    // their derived HP / stamina maxima read the JSON-tuned
    // coefficients (hp_per_vig, stamina_per_end). Falls back silently
    // to struct defaults if the file is missing or malformed.
    selva::tuning::loadFromFile(kTunablesPath);

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

        // Phase-0 audit for root-motion refactor: dump per-clip hip
        // path length so we know which clips ship with authored
        // translation.
        selva::anim::auditClipHipMotion();
        selva::gameplay::initHubEnemies();
    }

    // Per-clip locomotion blend-in durations.
    selva::anim::locomotionConfig().loadFromFile("config/locomotion.json");

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

    engine.setPerFrameUpdate(&selva::gameplay::selvaPerFrame);
    engine.setRenderWorld(&selva::gameplay::selvaRenderWorld);
    engine.setRenderImGui(&selva::ui::selvaRenderImGui);
    engine.setOnResize(&selva::render::onWindowResize);

    engine.run();

    selva::gameplay::shutdownHubEnemies();
    selva::anim::shutdownSkeletalAssets();
    shutdownGeometry();
    engine.shutdown();
    selva::combat::closeCombatLog();
    return 0;
}
