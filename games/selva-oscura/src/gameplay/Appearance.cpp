#include "gameplay/Appearance.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace selva::gameplay
{

Appearance loadAppearance(const std::string& path)
{
    Appearance out;
    if (path.empty())
        return out;
    if (!std::filesystem::exists(path))
    {
        std::fprintf(stderr, "[appearance] config not found: %s (using default body_scale=1.0)\n",
                     path.c_str());
        std::fflush(stderr);
        return out;
    }
    std::ifstream f(path);
    if (!f.is_open())
    {
        std::fprintf(stderr, "[appearance] failed to open: %s\n", path.c_str());
        std::fflush(stderr);
        return out;
    }
    nlohmann::json j;
    try
    {
        f >> j;
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "[appearance] parse error in %s: %s\n", path.c_str(), e.what());
        std::fflush(stderr);
        return out;
    }
    if (j.contains("body_scale") && j["body_scale"].is_number())
        out.body_scale = j["body_scale"].get<float>();
    if (j.contains("head_scale") && j["head_scale"].is_number())
        out.head_scale = j["head_scale"].get<float>();
    if (j.contains("arm_scale") && j["arm_scale"].is_number())
        out.arm_scale = j["arm_scale"].get<float>();
    if (j.contains("leg_scale") && j["leg_scale"].is_number())
        out.leg_scale = j["leg_scale"].get<float>();
    if (j.contains("torso_scale") && j["torso_scale"].is_number())
        out.torso_scale = j["torso_scale"].get<float>();
    if (j.contains("color") && j["color"].is_array() && j["color"].size() == 3)
    {
        // Souls-convention RGB: clamp each channel to [0, 1] so
        // out-of-range values don't blow out the lighting. Log when a
        // clamp actually fires so authoring mistakes ("color":
        // [255, 0, 0] vs the normalized form) are visible.
        for (int i = 0; i < 3; ++i)
        {
            const float raw = j["color"][i].get<float>();
            const float clamped = std::clamp(raw, 0.0f, 1.0f);
            if (clamped != raw)
            {
                std::fprintf(stderr,
                             "[appearance] %s: color[%d]=%.3f clamped to %.3f "
                             "(expected normalized 0..1)\n",
                             path.c_str(), i, raw, clamped);
                std::fflush(stderr);
            }
            out.color[i] = clamped;
        }
    }
    return out;
}

bool saveAppearance(const std::string& path, const Appearance& appearance)
{
    if (path.empty())
    {
        std::fprintf(stderr, "[appearance] save called with empty path\n");
        std::fflush(stderr);
        return false;
    }
    nlohmann::json j;
    j["body_scale"] = appearance.body_scale;
    j["head_scale"] = appearance.head_scale;
    j["arm_scale"] = appearance.arm_scale;
    j["leg_scale"] = appearance.leg_scale;
    j["torso_scale"] = appearance.torso_scale;
    j["color"] = {appearance.color.x, appearance.color.y, appearance.color.z};
    std::ofstream f(path);
    if (!f.is_open())
    {
        std::fprintf(stderr, "[appearance] failed to open for write: %s\n", path.c_str());
        std::fflush(stderr);
        return false;
    }
    f << j.dump(2) << '\n';
    return true;
}

} // namespace selva::gameplay
