#include "systems/SpriteAnimSystem.h"

#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "ops/LogUtils.h"

#include <entt/entt.hpp>

namespace sprite_anim
{
namespace
{

// The tag playing, and the ranges available. Kept beside the engine's Animation rather than
// inside it: the engine addresses frames by number and has no notion of a name.
struct AnimSet
{
    std::vector<sprite_def::Anim> anims;
    std::vector<int> durations_ms;
    std::string playing;
};

const std::string kNone;

// Aseprite gives a duration per FRAME; the engine takes one duration per animation. Playing a
// cycle at its slowest frame would drag, and at its fastest would gallop, so the average is
// what preserves the overall pace the timeline was authored at. A cycle needing genuinely
// uneven frames (a held pose, an anticipation beat) wants per-frame timing in the engine --
// worth doing when art asks for it, not before.
float averageDuration(const AnimSet& set, const sprite_def::Anim& anim)
{
    int total = 0;
    int count = 0;
    for (int f = anim.from; f <= anim.to && f < static_cast<int>(set.durations_ms.size()); ++f)
    {
        total += set.durations_ms[static_cast<size_t>(f)];
        ++count;
    }
    return count > 0 ? static_cast<float>(total) / static_cast<float>(count) / 1000.0f : 0.0f;
}

} // namespace

void attach(EntityManager& em, entt::entity entity, const sprite_def::Def& def)
{
    if (def.anims.empty())
        return;

    auto& reg = em.registry();
    auto& set = reg.emplace_or_replace<AnimSet>(entity);
    set.anims = def.anims;
    set.durations_ms = def.durations_ms;

    auto& anim = reg.emplace_or_replace<Animation>(entity);
    anim.frame_width = def.frame_w;
    anim.frame_height = def.frame_h;
    // 2 = one drawing, mirrored: the engine flips it for West and holds the last side when he
    // walks straight up or down. NOT 1, which means "omnidirectional" and forces flip off.
    anim.direction_count = 2;
    anim.row_count = 1;
    anim.current_row = 0;
    anim.max_frames_per_state = def.frames;

    // WALKING IS WHAT A BODY DOES. Starting on whichever tag happens to sit first in the file
    // made a one-tag pest work by accident and left a two-tag one holding its idle forever
    // -- and idle is a pose for LOOKING at (the field guide's plate), not a state anything
    // spends its life in.
    const bool walks = std::any_of(def.anims.begin(), def.anims.end(),
                                   [](const sprite_def::Anim& a) { return a.name == "walk"; });
    play(em, entity, walks ? std::string{"walk"} : def.anims.front().name);
}

void play(EntityManager& em, entt::entity entity, const std::string& name)
{
    auto& reg = em.registry();
    if (!reg.all_of<AnimSet, Animation>(entity))
        return;

    auto& set = reg.get<AnimSet>(entity);
    if (set.playing == name)
        return;

    const auto it = std::find_if(set.anims.begin(), set.anims.end(),
                                 [&](const sprite_def::Anim& a) { return a.name == name; });
    if (it == set.anims.end())
    {
        poe::log().warn("anim: no tag '{}' on this sprite -- keeping '{}'", name, set.playing);
        return;
    }

    // The mask carries the tag's columns, which is what lets many named animations share one
    // row. The engine resets the frame index when the ROW changes; the row never changes here,
    // so the reset is ours to do.
    auto& anim = reg.get<Animation>(entity);
    anim.frame_mask.clear();
    for (int f = it->from; f <= it->to; ++f)
        anim.frame_mask.push_back(f);
    anim.current_frames = static_cast<int>(anim.frame_mask.size());
    // A single-frame tag is a held pose, not a one-frame loop: 0 tells the engine to stop
    // advancing rather than tick a timer that can never change what is on screen.
    anim.current_duration = anim.current_frames > 1 ? averageDuration(set, *it) : 0.0f;
    anim.frame_index = 0;
    anim.frame_timer = 0.0f;
    set.playing = name;
}

void playIfPresent(EntityManager& em, entt::entity entity, const std::string& name,
                   const std::string& fallback)
{
    auto& reg = em.registry();
    if (!reg.all_of<AnimSet, Animation>(entity))
        return;

    const auto& set = reg.get<AnimSet>(entity);
    const bool have = std::any_of(set.anims.begin(), set.anims.end(),
                                  [&](const sprite_def::Anim& a) { return a.name == name; });
    if (have)
    {
        play(em, entity, name);
        return;
    }

    // Standing on the fallback's first frame, held. Recorded under the name ASKED for, not the
    // one borrowed, so asking for "walk" again is a real change and starts the cycle moving --
    // recording it as "walk" would leave the character frozen the moment he stepped off.
    if (set.playing == name)
        return;
    play(em, entity, fallback);
    auto& anim = reg.get<Animation>(entity);
    anim.current_duration = 0.0f; // 0 = static; the engine stops advancing frames
    anim.frame_index = 0;
    reg.get<AnimSet>(entity).playing = name;
}

const std::string& current(const EntityManager& em, entt::entity entity)
{
    const auto& reg = em.registry();
    return reg.all_of<AnimSet>(entity) ? reg.get<AnimSet>(entity).playing : kNone;
}

} // namespace sprite_anim
