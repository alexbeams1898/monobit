#include "anim/Skeleton.h"

#include <ozz/animation/runtime/skeleton.h>
#include <ozz/base/io/archive.h>
#include <ozz/base/io/stream.h>

#include <cstdio>

namespace selva::anim
{

// Out-of-line special members so the unique_ptr<ozz::animation::Skeleton>
// destructor sees the complete type. Without these, the implicit defaults
// in the header would try to instantiate ~Skeleton at every include site
// where the type is forward-declared but incomplete.
Skeleton::Skeleton() = default;
Skeleton::Skeleton(Skeleton&&) noexcept = default;
Skeleton& Skeleton::operator=(Skeleton&&) noexcept = default;
Skeleton::~Skeleton() = default;

int Skeleton::boneCount() const
{
    return ozz_skeleton ? ozz_skeleton->num_joints() : 0;
}

bool Skeleton::isLoaded() const
{
    return ozz_skeleton && ozz_skeleton->num_joints() > 0;
}

Skeleton loadSkeleton(const std::string& path)
{
    Skeleton out;

    ozz::io::File file(path.c_str(), "rb");
    if (!file.opened())
    {
        std::fprintf(stderr, "[Skeleton] cannot open %s\n", path.c_str());
        return out;
    }

    ozz::io::IArchive archive(&file);
    if (!archive.TestTag<ozz::animation::Skeleton>())
    {
        std::fprintf(stderr, "[Skeleton] %s is not a skeleton archive\n", path.c_str());
        return out;
    }

    out.ozz_skeleton = std::make_unique<ozz::animation::Skeleton>();
    archive >> *out.ozz_skeleton;
    return out;
}

} // namespace selva::anim
