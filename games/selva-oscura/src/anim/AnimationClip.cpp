#include "anim/AnimationClip.h"

#include <cstdio>
#include <ozz/animation/runtime/animation.h>
#include <ozz/base/io/archive.h>
#include <ozz/base/io/stream.h>

namespace selva::anim
{

AnimationClip::AnimationClip() = default;
AnimationClip::AnimationClip(AnimationClip&&) noexcept = default;
AnimationClip& AnimationClip::operator=(AnimationClip&&) noexcept = default;
AnimationClip::~AnimationClip() = default;

float AnimationClip::duration() const
{
    return ozz_animation ? ozz_animation->duration() : 0.0f;
}

int AnimationClip::trackCount() const
{
    return ozz_animation ? ozz_animation->num_tracks() : 0;
}

bool AnimationClip::isLoaded() const
{
    return ozz_animation && ozz_animation->num_tracks() > 0;
}

AnimationClip loadAnimationClip(const std::string& path)
{
    AnimationClip out;

    ozz::io::File file(path.c_str(), "rb");
    if (!file.opened())
    {
        std::fprintf(stderr, "[AnimationClip] cannot open %s\n", path.c_str());
        return out;
    }

    ozz::io::IArchive archive(&file);
    if (!archive.TestTag<ozz::animation::Animation>())
    {
        std::fprintf(stderr, "[AnimationClip] %s is not an animation archive\n", path.c_str());
        return out;
    }

    out.ozz_animation = std::make_unique<ozz::animation::Animation>();
    archive >> *out.ozz_animation;
    return out;
}

} // namespace selva::anim
