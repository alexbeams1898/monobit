#include "gameplay/ScriptedEvents.h"

#include "AppState.h"
#include "AppStateGlobal.h"
#include "Scene.h"
#include "combat/CombatLog.h"
#include "gameplay/Actor.h"
#include "gameplay/Enemies.h"
#include "items/InventoryOps.h"
#include "world/Door.h"

#include <cmath>
#include <cstdio>
#include <limits>

namespace selva::gameplay
{

namespace
{

// Find the Guide actor in the pool. Returns nullptr if not spawned
// (e.g. region not active yet, or actor was never spawned for this
// save).
Actor* findGuideActor()
{
    for (auto& a : actors())
    {
        if (a.spawn_decl_id == "guide")
            return &a;
    }
    return nullptr;
}

// Guide-rescue event. Fires once when the `lupa_felled` flag is set
// (either path through fireEnemyDeath). Opens the chapel door and
// drives the Guide toward the player under a Scene input-lock.
// Flags persist across save/load.

bool guideRescuePrecondition()
{
    if (hasFlag("guide_emerged"))
        return false; // Already fired (this session OR a prior session).
    return hasFlag("lupa_felled");
}

void beginGuideRescueScene()
{
    setFlag("guide_emerged");
    if (!selva::scene::active())
        selva::scene::begin({/*combat=*/true, /*movement=*/true, /*look=*/false});

    selva::world::openDoor("chapel_front_door");

    Actor* guide = findGuideActor();
    if (guide == nullptr)
    {
        // No actor with the expected spawn id; mark arrived and bail
        // so the event doesn't loop.
        std::fprintf(stderr,
                     "[scripted-event] guide_rescue: no Guide actor in pool; "
                     "skipping walk-out\n");
        std::fflush(stderr);
        setFlag("guide_arrived");
        if (selva::scene::active())
            selva::scene::end();
        return;
    }
    const Actor& pc = player();
    guide->scripted_target_pos = pc.pos;
    guide->scripted_stop_range = 2.0f;
    std::fprintf(stderr,
                 "[scripted-event] guide_rescue: BEGIN -- door opening, Guide -> "
                 "(%.2f, %.2f, %.2f)\n",
                 pc.pos.x, pc.pos.y, pc.pos.z);
    std::fflush(stderr);
}

// Per-frame advance: if the Scene is active AND the Guide has cleared
// his scripted_target_pos (LeafFollowScriptedTarget arrived), end the
// Scene. Re-targets each frame for the live player position so the
// Guide tracks a moving player instead of walking to a stale point.
void advanceGuideRescue()
{
    if (!hasFlag("guide_emerged") || hasFlag("guide_arrived"))
        return;
    Actor* guide = findGuideActor();
    if (guide == nullptr)
        return;
    if (std::isnan(guide->scripted_target_pos.x))
    {
        // LeafFollowScriptedTarget cleared the target -- arrived.
        setFlag("guide_arrived");
        // Grant the Seal. Dialog will eventually intermediate this
        // (Guide explains, player accepts); for now grant on arrival.
        if (PlayerProfile* profile = selva::activePlayerProfile())
        {
            const bool granted = selva::items::grant(profile->inventory, "seal");
            std::fprintf(stderr, "[scripted-event] guide_rescue: granted seal (new=%d)\n",
                         granted ? 1 : 0);
            std::fflush(stderr);
        }
        selva::scene::end();
        std::fprintf(stderr, "[scripted-event] guide_rescue: END -- Guide arrived\n");
        std::fflush(stderr);
        return;
    }
    // Live-update the target to the current player position so the
    // Guide tracks even if the player moves during the Scene.
    guide->scripted_target_pos = player().pos;
}

} // namespace

void tickScriptedEvents()
{
    if (guideRescuePrecondition())
        beginGuideRescueScene();
    advanceGuideRescue();
}

} // namespace selva::gameplay
