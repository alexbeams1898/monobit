#include "anim/SkeletonJointMap.h"

#include "anim/SkeletalAssets.h" // for kPlayerSkeletonKey

#include <nlohmann/json.hpp>

#include <cstdio>
#include <fstream>
#include <unordered_map>

namespace selva::anim
{

namespace
{

std::unordered_map<std::string, SkeletonJointMap> sMaps;
SkeletonJointMap sEmpty; // fallback for missing keys (player resolves first)

void parseInto(SkeletonJointMap& out, const nlohmann::json& doc)
{
    if (!doc.contains("joint_names"))
        return;
    const auto& names = doc.at("joint_names");
    auto str = [&](const char* key) -> std::string
    {
        if (names.contains(key) && names.at(key).is_string())
            return names.at(key).get<std::string>();
        return std::string{};
    };
    out.hips = str("hips");
    out.upleg_left = str("upleg_left");
    out.upleg_right = str("upleg_right");
    out.leg_left = str("leg_left");
    out.leg_right = str("leg_right");
    out.foot_left = str("foot_left");
    out.foot_right = str("foot_right");
    out.head = str("head");
    // Default lockon points for any archetype on this skeleton that
    // doesn't author its own list. Wolf's wolf.json overrides this
    // entirely (head/torso/hindleg_*). Skeleton JSON shape:
    //   "lockon_points": [
    //     { "id": "chest", "joint": "Torso2", "default": true }
    //   ]
    out.default_lockon_points.clear();
    if (doc.contains("lockon_points") && doc.at("lockon_points").is_array())
    {
        for (const auto& p : doc.at("lockon_points"))
        {
            LockOnPointDecl d;
            d.id = p.value("id", std::string{});
            d.joint = p.value("joint", std::string{});
            d.is_default = p.value("default", false);
            if (!d.joint.empty())
                out.default_lockon_points.push_back(std::move(d));
        }
    }
}

} // namespace

SkeletonJointMap loadSkeletonJointMap(const std::string& id)
{
    SkeletonJointMap map;
    map.skeleton_id = id;
    const std::string path = "config/skeletons/" + id + ".json";
    std::ifstream f(path);
    if (!f.is_open())
    {
        std::fprintf(stderr, "[anim] skeleton joint map missing: %s\n", path.c_str());
        return map;
    }
    try
    {
        nlohmann::json doc;
        f >> doc;
        parseInto(map, doc);
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "[anim] failed to parse %s: %s\n", path.c_str(), e.what());
    }
    return map;
}

const SkeletonJointMap& jointMapByKey(const std::string& key)
{
    auto it = sMaps.find(key);
    if (it != sMaps.end())
        return it->second;
    auto player_it = sMaps.find(std::string(kPlayerSkeletonKey));
    if (player_it != sMaps.end())
        return player_it->second;
    return sEmpty;
}

void loadAllSkeletonJointMaps()
{
    // Player map is required for the existing X_Bot rig.
    sMaps[std::string(kPlayerSkeletonKey)] = loadSkeletonJointMap(std::string(kPlayerSkeletonKey));
    // Non-player maps are best-effort. Wolf is the v1 extra skeleton.
    static const char* kExtraSkeletons[] = {"wolf"};
    for (const char* id : kExtraSkeletons)
    {
        auto m = loadSkeletonJointMap(std::string(id));
        // Only register if hips at least loaded -- otherwise the file
        // was missing or malformed; the empty map is unusable.
        if (!m.hips.empty())
            sMaps[std::string(id)] = std::move(m);
    }
}

} // namespace selva::anim
