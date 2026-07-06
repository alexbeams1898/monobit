// Boot-time registration of the built-in AppearanceTransformWriter
// sources. Lives in its own translation unit so main.cpp doesn't
// have to pull in EnemyArchetype + WallClock + the lambda bodies
// (and so future sources owned by other systems can sit next to
// their owning system, with main.cpp just calling one init each).
//
// Today registers only `archetype_transform` -- the generalized
// drop-in for the legacy EnemyArchetype.transform_target_archetype
// behavior (fresh->aged larva burn). Player-side sources land here
// (or in their owning system's init) as they get authored.

#include "WallClock.h"
#include "gameplay/Actor.h"
#include "gameplay/AppearanceTransformWriter.h"
#include "gameplay/EnemyArchetype.h"

#include <algorithm>

namespace selva::gameplay
{

void initBuiltinAppearanceTransformSources()
{
    // The archetype_transform source: legacy "this archetype becomes
    // that archetype over the arrival-wait period" behavior. Strength
    // ramps from 0 to 1 across (arrival_wallclock + 0) ->
    // (arrival_wallclock + arrival_action_delay_seconds). Used today
    // for larva fresh->aged burn-in; mechanism stays available for
    // any archetype that declares a transform_target_archetype.
    //
    // active() returns true only when the archetype declares a
    // transform target AND the actor has been seated with timing
    // data (the spawn-flow stamps arrival_wallclock when the actor
    // arrives at its scripted destination).
    TransformSource archetype_transform;
    archetype_transform.id = "archetype_transform";
    archetype_transform.source_kind = "archetype_transform";
    archetype_transform.active = [](const Actor& a) -> bool
    {
        if (a.archetype == nullptr)
            return false;
        if (a.archetype->transform_target_archetype.empty())
            return false;
        if (a.arrival_wallclock <= 0.0f)
            return false;
        if (a.arrival_action_delay_seconds <= 0.0f)
            return false;
        return true;
    };
    archetype_transform.strength = [](const Actor& a) -> float
    {
        const float elapsed = selva::wallClock() - a.arrival_wallclock;
        const float t = elapsed / a.arrival_action_delay_seconds;
        return std::clamp(t, 0.0f, 1.0f);
    };
    // Per-actor target resolver -- the snapshot path is the
    // appearance file of the archetype the actor is transforming
    // INTO. The reconciler calls this every frame the source is
    // active.
    archetype_transform.target_path = [](const Actor& a) -> std::string
    {
        if (a.archetype == nullptr)
            return {};
        const EnemyArchetype* target = archetypes().get(a.archetype->transform_target_archetype);
        if (target == nullptr)
            return {};
        return target->character_path;
    };

    registerTransformSource(std::move(archetype_transform));

    // Additional TransformSources land here as we add them. The
    // pattern: a TransformSource declares `active(actor)` + `strength(actor)`
    // pure-derivation callbacks, plus a `target_snapshot_path` to a
    // snapshot in `config/appearances/transforms/`. Per-system init
    // code can also register sources from its own boot site -- the
    // registry is process-wide. See [docs/design/character-canvas.md
    // *The AppearanceTransform primitive*] for the doctrine.
}

} // namespace selva::gameplay
