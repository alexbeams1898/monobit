#include "JsonConfig.h"

#include <fstream>

namespace config
{

std::optional<nlohmann::json> load(const std::string& path)
{
    std::ifstream f(path);
    if (!f)
        return std::nullopt;
    nlohmann::json j = nlohmann::json::parse(f, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded())
        return std::nullopt;
    return j;
}

} // namespace config
