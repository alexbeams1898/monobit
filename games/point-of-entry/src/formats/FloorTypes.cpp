#include "formats/FloorTypes.h"

#include "ops/LogUtils.h"

#include <nlohmann/json.hpp>

#include <fstream>

namespace formats
{
namespace
{
// A base naming itself, or a ring of them, would recurse until the stack ran out.
constexpr int kMaxBaseChain = 8;

nlohmann::json readAt(const std::string& path, int depth)
{
    std::ifstream in(path);
    if (!in)
    {
        poe::log().warn("floor: no floor type at '{}' -- every reader falls back", path);
        return nlohmann::json::object();
    }
    nlohmann::json j = nlohmann::json::parse(in, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.is_object())
    {
        poe::log().warn("floor: floor type at '{}' is not valid JSON -- every reader falls back",
                        path);
        return nlohmann::json::object();
    }
    const std::string base = j.value("base", std::string{});
    if (base.empty() || base == path || depth >= kMaxBaseChain)
        return j;
    nlohmann::json out = readAt(base, depth + 1);
    out.merge_patch(j);
    return out;
}
} // namespace

nlohmann::json read(const std::string& path)
{
    return readAt(path, 0);
}

} // namespace formats
