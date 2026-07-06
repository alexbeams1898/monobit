#pragma once

#include <functional>
#include <string>
#include <vector>

namespace selva::gameplay
{

struct Actor; // forward; full def in gameplay/Actor.h

// One registered "thing in the world that can change a body" --
// declaratively wired to its trigger condition + strength function.
// Each TransformSource produces ZERO OR ONE AppearanceTransform on
// any given actor per frame: either the source is `active` for the
// actor (the transform appears in actor.appearance.transforms with
// the current `strength`), or it's not (the transform is absent).
//
// Determinism contract: BOTH `active` and `strength` MUST be pure
// functions of the actor + world state. The reconciler rebuilds
// every actor's transforms list from scratch each frame via these
// callbacks; if they're non-pure, save/load would diverge from the
// pre-save state.
//
// Sources are registered at boot by the systems that own the
// triggering flag/stat. SangueSystem::init() registers a source for
// sangue_bloat. EvolutionSystem::init() registers one per evolution
// node. The Appearance system doesn't know what triggers exist; it
// only knows how to apply them.
//
// Design: [docs/design/character-canvas.md](docs/design/character-canvas.md)
// *The AppearanceTransform primitive*.
struct TransformSource
{
    // Stable id matching the snapshot file basename (`sangue_bloat` ->
    // config/appearances/transforms/sangue_bloat.json). Also the key
    // the reconciler uses to deduplicate: registering two sources
    // with the same id is a bug (registration logs + drops the
    // second).
    std::string id;

    // Free-form category for diagnostics / future debug-UI ("why does
    // the player look this way?"). Not consumed by the resolver.
    // Examples: "flag", "stat_threshold", "residence_time",
    // "evolution_node", "boss_felled", "archetype_transform".
    std::string source_kind;

    // Static snapshot path -- config/appearances/transforms/<id>.json
    // -- the same target for every actor this source touches. Set
    // for sources whose target doesn't depend on the actor (most
    // sources). Mutually exclusive with `target_path` below: set
    // EITHER this OR target_path, not both.
    std::string target_snapshot_path;

    // Per-actor target resolver. Set for sources whose target varies
    // per actor -- e.g. the archetype_transform source whose target
    // is determined by actor.archetype->transform_target_archetype.
    // Called every frame the source is active; should be cheap and
    // pure. Mutually exclusive with target_snapshot_path; when set,
    // overrides it.
    std::function<std::string(const Actor&)> target_path;

    // Does this source CURRENTLY apply to this actor? Called per
    // actor per frame. Return false to suppress the transform.
    std::function<bool(const Actor&)> active;

    // 0..1 lerp weight, called ONLY when `active` returned true.
    // Continuous-state sources return a function of the actor's
    // current state (e.g. sangue_bloat -> f(sangue_load));
    // binary-state sources return 1.0.
    std::function<float(const Actor&)> strength;
};

// Register a TransformSource with the process-wide writer. Call at
// boot from the system that owns the triggering condition. Safe to
// call before or after AppearanceTransformWriter is first ticked.
// Duplicate ids are dropped with a stderr log.
void registerTransformSource(TransformSource source);

// Per-actor per-frame reconciliation. Walks the registered sources,
// evaluates each against the actor, and rebuilds
// actor.appearance.transforms as a pure derivation. Cheap: O(N
// sources). Call from the per-frame tick (player tick + enemy tick).
void reconcileAppearanceTransforms(Actor& actor);

// Clear the registry. Used by tests + by hot-reload paths that
// re-init gameplay systems mid-process. Production code doesn't
// call this.
void clearTransformSources();

// Number of currently-registered sources. For diagnostics + tests.
std::size_t transformSourceCount();

// Boot-time entry point that registers the engine's built-in
// TransformSources (archetype_transform today; more as systems
// migrate their appearance-affecting flags into the new model).
// Call from main.cpp's boot sequence ONCE, after the appearance
// slider registry + archetype registry are loaded. Idempotent at
// the source level (each source's id is dedup'd at registration).
void initBuiltinAppearanceTransformSources();

} // namespace selva::gameplay
