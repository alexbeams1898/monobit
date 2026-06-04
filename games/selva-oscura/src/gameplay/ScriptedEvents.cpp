#include "gameplay/ScriptedEvents.h"

#include "AppState.h"
#include "AppStateGlobal.h"
#include "Scene.h"
#include "combat/CombatLog.h"
#include "dialog/DialogSystem.h"
#include "gameplay/Actor.h"
#include "gameplay/Enemies.h"
#include "text/TextPresentation.h"
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

// State machine driven entirely by PlayerProfile flags so the phase
// survives save/load. Flags advance one direction; the event can
// never regress. Phases:
//   1. (precondition met)         -> setFlag guide_emerged
//                                    open door, begin Scene, Guide idle
//   2. door reaches Open          -> setFlag guide_door_open
//                                    set Guide's scripted_target_pos
//   3. Guide reaches player       -> setFlag guide_arrived
//                                    dialog::begin
//   4. dialog ends                -> scene::end (terminal)
void beginGuideRescueScene()
{
    setFlag("guide_emerged");
    if (!selva::scene::active())
        selva::scene::begin({/*combat=*/true, /*movement=*/true, /*look=*/false});
    selva::world::openDoor("chapel_front_door");
    std::fprintf(stderr, "[scripted-event] guide_rescue: phase=emerged (door opening)\n");
    std::fflush(stderr);
}

void advanceGuideRescue()
{
    if (!hasFlag("guide_emerged"))
        return;

    // Phase 4: dialog drives the Scene end.
    if (hasFlag("guide_arrived"))
    {
        if (selva::scene::active() && !selva::text::active())
        {
            selva::scene::end();
            std::fprintf(stderr, "[scripted-event] guide_rescue: phase=complete (dialog ended)\n");
            std::fflush(stderr);
            // Phase 5: if the Signing was committed, send the Guide
            // to his post-Signing standing post (declared on his
            // spawn record's post_flag_positions). Reads the first
            // matching entry so the position lives in JSON, not
            // hard-coded here.
            if (hasFlag("signing_committed"))
            {
                Actor* guide = findGuideActor();
                if (guide != nullptr)
                {
                    for (const auto& fp : guide->post_flag_positions)
                    {
                        if (fp.flag != "signing_committed")
                            continue;
                        guide->scripted_target_pos = fp.pos;
                        guide->scripted_stop_range = 0.4f;
                        std::fprintf(stderr,
                                     "[scripted-event] guide_rescue: walking to "
                                     "post-Signing post (%.2f,%.2f,%.2f)\n",
                                     fp.pos.x, fp.pos.y, fp.pos.z);
                        std::fflush(stderr);
                        break;
                    }
                }
            }
        }
        return;
    }

    Actor* guide = findGuideActor();
    if (guide == nullptr)
    {
        // Defensive: no guide actor in the pool. Skip phases 2-4 so
        // the event never deadlocks the Scene.
        std::fprintf(stderr, "[scripted-event] guide_rescue: no Guide actor in pool; "
                             "skipping walk-out\n");
        std::fflush(stderr);
        setFlag("guide_arrived");
        if (selva::scene::active())
            selva::scene::end();
        return;
    }

    // Phase 2: wait for door to finish opening before sending the
    // Guide through. Without this the Guide collides with the
    // closed-then-opening door's collider and curves around it.
    if (!hasFlag("guide_door_open"))
    {
        const selva::world::Door* door = selva::world::findDoor("chapel_front_door");
        if (door == nullptr || door->state == selva::world::DoorState::Open)
        {
            setFlag("guide_door_open");
            const Actor& pc = player();
            guide->scripted_target_pos = pc.pos;
            guide->scripted_stop_range = 2.0f;
            std::fprintf(stderr,
                         "[scripted-event] guide_rescue: phase=door_open "
                         "(Guide -> %.2f,%.2f,%.2f)\n",
                         pc.pos.x, pc.pos.y, pc.pos.z);
            std::fflush(stderr);
        }
        return;
    }

    // Phase 3: Guide is walking. If he reached the target, advance.
    if (std::isnan(guide->scripted_target_pos.x))
    {
        setFlag("guide_arrived");
        selva::dialog::begin("guide");
        std::fprintf(stderr, "[scripted-event] guide_rescue: phase=arrived (dialog open)\n");
        std::fflush(stderr);
        return;
    }
    // Live-update the target so the Guide tracks a moving player.
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
