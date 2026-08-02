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

Color readColor(const nlohmann::json& j, const char* key, const Color& fallback)
{
    const auto it = j.find(key);
    if (it == j.end() || !it->is_array() || it->size() != 4)
        return fallback;
    for (const auto& c : *it)
        if (!c.is_number())
            return fallback; // a non-number would throw on .get<float>()
    return {(*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>(),
            (*it)[3].get<float>()};
}

} // namespace config
