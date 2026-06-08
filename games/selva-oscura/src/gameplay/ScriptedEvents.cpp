#include "gameplay/ScriptedEvents.h"

#include "AppState.h"
#include "AppStateGlobal.h"
#include "Scene.h"
#include "combat/CombatLog.h"
#include "dialog/DialogSystem.h"
#include "gameplay/Actor.h"
#include "gameplay/Enemies.h"
#include "text/TextPresentation.h"
#include "ui/ClassPickerScreen.h"
#include "world/Door.h"

#include <cmath>
#include <cstdio>
#include <limits>

namespace selva::gameplay
{

namespace
{

Actor* findGuideActor()
{
    for (auto& a : actors())
    {
        if (a.spawn_decl_id == "guide")
            return &a;
    }
    return nullptr;
}

// Matches the NPC interact range from Enemies.cpp's
// applyArchetypeInteractable() default -- keep in sync.
constexpr float kForceEngageTalkRange = 2.5f;

// Phase A: unlock the chapel door on lupa_felled. Fires once.
void tickGuideUnlockChapelDoor()
{
    if (hasFlag("guide_unlocked_chapel"))
        return;
    if (!hasFlag("lupa_felled"))
        return;
    setFlag("guide_unlocked_chapel");
    selva::world::unlockDoor("chapel_front_door");
    std::fprintf(stderr, "[scripted-event] guide_unlocked_chapel (lock_unlock SFX)\n");
    std::fflush(stderr);
}

// Phase B: forced-engage intercept. Sends the Guide walking toward
// the player; arrival is detected by advanceGuideForceEngage.
// Skipped if the player is already talking to him via E-press
// (Enemies.cpp's interactable opens dialog directly, no Scene).
void beginGuideForceEngage()
{
    if (hasFlag("signing_committed"))
        return;
    if (hasFlag("guide_force_engage_active"))
        return;
    if (selva::dialog::active())
        return;
    Actor* guide = findGuideActor();
    if (guide == nullptr)
        return;
    setFlag("guide_force_engage_active");
    if (!selva::scene::active())
        selva::scene::begin({/*combat=*/true, /*movement=*/true, /*look=*/false});
    // Chapel interior is unobstructed between Guide spawn and the
    // forced-engage trigger -- single-leg walk, no waypoints.
    guide->scripted_target_pos = player().pos;
    guide->scripted_path_waypoints.clear();
    guide->scripted_stop_range = kForceEngageTalkRange;
    std::fprintf(stderr, "[scripted-event] guide_force_engage: begin intercept\n");
    std::fflush(stderr);
}

// Phase C: tracks the Guide-to-player distance during an active
// force-engage. Opens dialog when within talk-range and stops the
// walk; otherwise live-updates the walk target so he tracks a
// moving player.
void advanceGuideForceEngage()
{
    if (!hasFlag("guide_force_engage_active"))
        return;
    if (hasFlag("signing_committed"))
        return;
    Actor* guide = findGuideActor();
    if (guide == nullptr)
        return;
    if (selva::dialog::active())
        return;

    const Actor& pc = player();
    const float dx = guide->pos.x - pc.pos.x;
    const float dz = guide->pos.z - pc.pos.z;
    const float dist_sq = dx * dx + dz * dz;
    if (dist_sq <= kForceEngageTalkRange * kForceEngageTalkRange)
    {
        guide->scripted_target_pos = glm::vec3(std::numeric_limits<float>::quiet_NaN(),
                                               std::numeric_limits<float>::quiet_NaN(),
                                               std::numeric_limits<float>::quiet_NaN());
        guide->scripted_path_waypoints.clear();
        selva::dialog::begin("guide");
        std::fprintf(stderr, "[scripted-event] guide_force_engage: dialog opened\n");
        std::fflush(stderr);
        return;
    }
    guide->scripted_target_pos = pc.pos;
}

// Phase D: post-Signing cleanup + open first-words dialog. Waits
// for the picker modal to close so the dialog panel doesn't visually
// overlap. Guide stays at the Signing position -- no walk-out (avoids
// pathing failure modes during a quiet moment).
void tickGuidePostSigning()
{
    if (!hasFlag("signing_committed"))
        return;
    if (hasFlag("post_signing_dialog_dispatched"))
        return;
    if (selva::ui::classPickerActive())
        return;
    if (selva::dialog::active())
        return;
    setFlag("post_signing_dialog_dispatched");
    clearFlag("guide_force_engage_active");
    if (selva::scene::active())
        selva::scene::end();
    selva::dialog::begin("guide");
    std::fprintf(stderr, "[scripted-event] guide_post_signing: opened first-words dialog\n");
    std::fflush(stderr);
}

} // namespace

// Public entry point invoked by the custom-trigger dispatcher when
// the player crosses chapel_interior::guide_force_engage.
void onForceEngageGuide()
{
    beginGuideForceEngage();
}

void tickScriptedEvents()
{
    tickGuideUnlockChapelDoor();
    advanceGuideForceEngage();
    tickGuidePostSigning();
}

} // namespace selva::gameplay
