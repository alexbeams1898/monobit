#include "gameplay/AppearanceTransformWriter.h"

#include "gameplay/Actor.h"
#include "gameplay/Appearance.h"

#include <algorithm>
#include <cstdio>

namespace selva::gameplay
{

namespace
{

// Process-wide registry. Vector (not map) because the per-actor
// reconcile walks ALL of them every frame -- linear scan is the
// access pattern; map would just add overhead per lookup. Source
// count is tens, not thousands.
std::vector<TransformSource>& registry()
{
    static std::vector<TransformSource> r;
    return r;
}

bool idAlreadyRegistered(const std::string& id)
{
    const auto& r = registry();
    return std::any_of(r.begin(), r.end(), [&](const TransformSource& s) { return s.id == id; });
}

} // namespace

void registerTransformSource(TransformSource source)
{
    if (source.id.empty())
    {
        std::fprintf(stderr, "[appearance-transform] rejected source: empty id\n");
        return;
    }
    if (!source.active || !source.strength)
    {
        std::fprintf(stderr,
                     "[appearance-transform] rejected source '%s': "
                     "missing active or strength function\n",
                     source.id.c_str());
        return;
    }
    if (idAlreadyRegistered(source.id))
    {
        std::fprintf(stderr,
                     "[appearance-transform] dropped duplicate source '%s' "
                     "(first registration wins)\n",
                     source.id.c_str());
        return;
    }
    registry().push_back(std::move(source));
}

void reconcileAppearanceTransforms(Actor& actor)
{
    // Rebuild from scratch every frame -- transforms is pure
    // derivation from world state; previous-frame content has no
    // authority. Reserve to dodge incremental realloc as we push.
    auto& list = actor.appearance.transforms;
    const std::size_t prev_count = list.size();
    list.clear();
    const auto& r = registry();
    list.reserve(r.size());

    for (const auto& src : r)
    {
        if (!src.active(actor))
            continue;
        AppearanceTransform xf;
        xf.id = src.id;
        xf.source_kind = src.source_kind;
        // Per-actor resolver wins when set; otherwise the static
        // snapshot path applies. Sources should set exactly one of
        // these (header contract). Empty result skips the transform
        // -- the resolver's path-empty check would short-circuit it
        // anyway, but we don't even push it.
        xf.target_snapshot_path =
            src.target_path ? src.target_path(actor) : src.target_snapshot_path;
        if (xf.target_snapshot_path.empty())
            continue;
        xf.strength = src.strength(actor);
        // Transform with zero (or near-zero) strength is harmless --
        // the resolver short-circuits it. Keeping the entry in the
        // list rather than filtering here keeps the diagnostic
        // "which sources fired this frame" answerable without
        // re-walking the registry.
        list.push_back(std::move(xf));
    }

    // Diagnostic: log every transform-list change so we can see
    // exactly when a transform first goes active or vanishes. Cheap
    // (only fires on state changes, not every frame). Helps catch
    // the case where a transform appears/disappears in lock-step
    // with a crash. Drop this once the appearance-transform system
    // proves itself stable.
    if (list.size() != prev_count)
    {
        std::fprintf(stderr, "[transform-reconcile] actor=%p count %zu -> %zu",
                     static_cast<const void*>(&actor), prev_count, list.size());
        for (const auto& xf : list)
            std::fprintf(stderr, " {%s s=%.3f path=%s}", xf.id.c_str(),
                         static_cast<double>(xf.strength), xf.target_snapshot_path.c_str());
        std::fprintf(stderr, "\n");
        std::fflush(stderr);
    }
}

void clearTransformSources()
{
    registry().clear();
}

std::size_t transformSourceCount()
{
    return registry().size();
}

} // namespace selva::gameplay
