#include "interact/Interaction.h"

#include "Scene.h"
#include "gameplay/Actor.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <unordered_map>

namespace selva::interact
{

const char* kindVerb(Kind k)
{
    switch (k)
    {
    case Kind::Talk:
        return "Talk to";
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
    bool has_target = false;
    TargetView target;
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
    if (s.has_target && s.target.id == id)
    {
        s.has_target = false;
        s.target = TargetView{};
    }
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
    // Suppress entirely during a cinematic Scene -- the world is in
    // input-locked mode; interaction prompts are not part of that
    // language.
    if (selva::scene::active())
    {
        if (s.has_target)
        {
            s.has_target = false;
            s.target = TargetView{};
        }
        return;
    }
    const glm::vec3 pc = selva::gameplay::player().pos;
    Id best_id = kInvalidId;
    float best_d2 = std::numeric_limits<float>::infinity();
    Kind best_kind = Kind::Custom;
    std::string best_label;
    glm::vec3 best_pos{0.0f};
    for (const auto& [id, entry] : s.by_id)
    {
        const Decl& d = entry.decl;
        if (d.available && !d.available())
            continue;
        const glm::vec3 pos = d.position();
        const float d2 = xzDistanceSquared(pos, pc);
        if (d2 > d.range_meters * d.range_meters)
            continue;
        if (d2 < best_d2)
        {
            best_d2 = d2;
            best_id = id;
            best_kind = d.kind;
            best_label = d.label;
            best_pos = pos;
        }
    }
    if (best_id == kInvalidId)
    {
        if (s.has_target)
        {
            s.has_target = false;
            s.target = TargetView{};
        }
        return;
    }
    s.has_target = true;
    s.target.id = best_id;
    s.target.kind = best_kind;
    s.target.label = std::move(best_label);
    s.target.world_pos = best_pos;
}

const TargetView* currentTarget()
{
    const auto& s = state();
    return s.has_target ? &s.target : nullptr;
}

void triggerCurrent()
{
    auto& s = state();
    if (!s.has_target)
        return;
    s.pending_fire_id = s.target.id;
    s.has_target = false;
    s.target = TargetView{};
}

void hardReset()
{
    auto& s = state();
    s.by_id.clear();
    s.has_target = false;
    s.target = TargetView{};
    s.next_id = 1;
    s.pending_fire_id = kInvalidId;
}

} // namespace selva::interact
