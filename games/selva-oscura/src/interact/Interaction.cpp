#include "interact/Interaction.h"

#include "Scene.h"
#include "gameplay/Actor.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <unordered_map>
#include <vector>

namespace selva::interact
{

// Verb shown in the "[E] <verb>" prompt. Verbs are bare -- no
// trailing "to" or noun -- because the prompt itself is now noun-
// stripped (the player is standing in front of the interactable; the
// noun would name things they might not have insight for yet).
const char* kindVerb(Kind k)
{
    switch (k)
    {
    case Kind::Talk:
        return "Talk";
    case Kind::Pickup:
        return "Pick up";
    case Kind::Examine:
        return "Examine";
    case Kind::Open:
        return "Open";
    case Kind::Use:
        return "Use";
    case Kind::Custom:
        return "";
    }
    return "";
}

namespace
{

struct RegistryEntry
{
    Decl decl;
};

struct State
{
    std::unordered_map<Id, RegistryEntry> by_id;
    Id next_id = 1;

    // All in-range candidates this frame, sorted closest-first by
    // XZ distance. Rebuilt every tick.
    std::vector<TargetView> candidates;

    // Id of the focused candidate carried over from the prior tick.
    // Used to keep selection sticky across frames; whenever this id
    // is still in `candidates`, it stays focused even if the
    // distance ordering has shifted. Set to kInvalidId means "no
    // sticky preference" (use index 0).
    Id sticky_focus_id = kInvalidId;

    // Index into `candidates` of the focused entry, or -1 if empty.
    // Updated by tick() (sticky resolution) and cycleFocus().
    int focus_index = -1;

    // Deferred-intent queue. triggerCurrent() sets pending_fire_id;
    // the next tick() consumes it and runs the on_interact handler.
    // Without deferral, the SAME E-press that triggers an interaction
    // also reaches the resulting dialog UI's IsKeyPressed check the
    // same frame -- closing the dialog before the player sees it.
    // Mirrors the same one-frame-deferred pattern dialog itself uses
    // for selectChoice/confirmAdvance. Generalizes to any future
    // interactable-kind whose on_interact opens an input-consuming
    // overlay.
    Id pending_fire_id = kInvalidId;
};

State& state()
{
    static State s;
    return s;
}

float xzDistanceSquared(const glm::vec3& a, const glm::vec3& b)
{
    const float dx = a.x - b.x;
    const float dz = a.z - b.z;
    return dx * dx + dz * dz;
}

} // namespace

Id registerInteractable(Decl decl)
{
    if (!decl.position || !decl.on_interact)
    {
        std::fprintf(stderr, "[interact] registerInteractable: missing position or on_interact\n");
        std::fflush(stderr);
        return kInvalidId;
    }
    auto& s = state();
    const Id id = s.next_id++;
    s.by_id[id] = RegistryEntry{std::move(decl)};
    return id;
}

void unregisterInteractable(Id id)
{
    if (id == kInvalidId)
        return;
    auto& s = state();
    s.by_id.erase(id);
    // Clear sticky focus if it referenced this id; the next tick
    // will rebuild candidates and resolve a new focus.
    if (s.sticky_focus_id == id)
        s.sticky_focus_id = kInvalidId;
}

void tick()
{
    auto& s = state();
    // Consume any pending fire-intent from a prior frame's
    // triggerCurrent. Deferred so the E-press that triggered the
    // interaction doesn't also reach whatever input-consuming
    // overlay the on_interact handler opens (dialog UI) the same
    // frame.
    if (s.pending_fire_id != kInvalidId)
    {
        const Id id = s.pending_fire_id;
        s.pending_fire_id = kInvalidId;
        const auto it = s.by_id.find(id);
        if (it != s.by_id.end())
        {
            const auto cb = it->second.decl.on_interact;
            if (cb)
                cb();
        }
    }

    s.candidates.clear();
    s.focus_index = -1;

    // Suppress entirely during a cinematic Scene -- the world is in
    // input-locked mode; interaction prompts are not part of that
    // language.
    if (selva::scene::active())
        return;

    const glm::vec3 pc = selva::gameplay::player().pos;

    // Collect every in-range, available interactable. Distance is
    // computed once per entry; we sort by it after the scan.
    struct ScratchEntry
    {
        Id id;
        Kind kind;
        std::string label;
        glm::vec3 world_pos;
        float d2;
    };
    std::vector<ScratchEntry> scratch;
    scratch.reserve(s.by_id.size());
    for (const auto& [id, entry] : s.by_id)
    {
        const Decl& d = entry.decl;
        if (d.available && !d.available())
            continue;
        const glm::vec3 pos = d.position();
        const float d2 = xzDistanceSquared(pos, pc);
        if (d2 > d.range_meters * d.range_meters)
            continue;
        scratch.push_back({id, d.kind, d.label, pos, d2});
    }
    std::sort(scratch.begin(), scratch.end(),
              [](const ScratchEntry& a, const ScratchEntry& b) { return a.d2 < b.d2; });

    s.candidates.reserve(scratch.size());
    for (auto& se : scratch)
    {
        TargetView tv;
        tv.id = se.id;
        tv.kind = se.kind;
        tv.label = std::move(se.label);
        tv.world_pos = se.world_pos;
        s.candidates.push_back(std::move(tv));
    }

    if (s.candidates.empty())
    {
        s.sticky_focus_id = kInvalidId;
        return;
    }

    // Sticky focus: if the prior frame's focused id is still in the
    // candidate list, keep it. Otherwise default to the closest.
    if (s.sticky_focus_id != kInvalidId)
    {
        for (size_t i = 0; i < s.candidates.size(); ++i)
        {
            if (s.candidates[i].id == s.sticky_focus_id)
            {
                s.focus_index = static_cast<int>(i);
                return;
            }
        }
    }
    s.focus_index = 0;
    s.sticky_focus_id = s.candidates[0].id;
}

const TargetView* currentTarget()
{
    const auto& s = state();
    if (s.focus_index < 0 || s.focus_index >= static_cast<int>(s.candidates.size()))
        return nullptr;
    return &s.candidates[static_cast<size_t>(s.focus_index)];
}

const std::vector<TargetView>& currentCandidates()
{
    return state().candidates;
}

int currentFocusIndex()
{
    return state().focus_index;
}

void cycleFocus()
{
    auto& s = state();
    if (s.candidates.size() < 2)
        return;
    s.focus_index = (s.focus_index + 1) % static_cast<int>(s.candidates.size());
    s.sticky_focus_id = s.candidates[static_cast<size_t>(s.focus_index)].id;
}

void triggerCurrent()
{
    auto& s = state();
    if (s.focus_index < 0 || s.focus_index >= static_cast<int>(s.candidates.size()))
        return;
    s.pending_fire_id = s.candidates[static_cast<size_t>(s.focus_index)].id;
    // Clear the focus + sticky pin so the prompt vanishes immediately
    // after the trigger press. tick() will rebuild on the next frame.
    s.focus_index = -1;
    s.candidates.clear();
    s.sticky_focus_id = kInvalidId;
}

void hardReset()
{
    auto& s = state();
    s.by_id.clear();
    s.candidates.clear();
    s.sticky_focus_id = kInvalidId;
    s.focus_index = -1;
    s.next_id = 1;
    s.pending_fire_id = kInvalidId;
}

} // namespace selva::interact
