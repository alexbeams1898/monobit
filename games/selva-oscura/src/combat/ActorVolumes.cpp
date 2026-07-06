#include "combat/ActorVolumes.h"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <unordered_set>

namespace selva::combat
{

namespace
{

glm::vec3 jointWorld(const selva::anim::PoseSampler& sampler, const glm::mat4& actor_model,
                     const char* name)
{
    const int idx = sampler.findJoint(name);
    if (idx < 0)
        return glm::vec3(actor_model[3]); // actor origin if joint missing
    const glm::vec3 local = sampler.jointWorldPos(idx);
    const glm::vec4 world = actor_model * glm::vec4(local, 1.0f);
    return glm::vec3(world);
}

// Build a region capsule between two joints. radius scales with
// the actor's collider_radius so larger bodies have larger
// hurtboxes without per-region tuning data yet.
Hurtbox capsuleBetween(const selva::anim::PoseSampler& sampler, const glm::mat4& actor_model,
                       const char* joint_a, const char* joint_b, HurtRegion region, float radius,
                       OwnerRef owner, selva::gameplay::Faction faction)
{
    Hurtbox h;
    h.shape.p0 = jointWorld(sampler, actor_model, joint_a);
    h.shape.p1 = jointWorld(sampler, actor_model, joint_b);
    h.shape.radius = radius;
    h.region = region;
    h.owner = owner;
    h.faction = faction;
    return h;
}

} // namespace

glm::mat4 buildActorModelMatrix(const glm::vec3& pos, float yaw, float foot_offset_y,
                                float body_scale)
{
    // pos.y is the actor's WORLD-space foot height (terrain Y at the
    // actor's XZ). Subtracting foot_offset_y * body_scale lifts the
    // mesh so its (scaled) model-space hip-Y lands on top of that
    // world foot height, putting the visible feet on the ground at
    // any elevation AND any body size. A 0.85-scale shade has shorter
    // legs in world units; foot_offset_y must shrink with them or the
    // feet sink below the terrain.
    const glm::mat4 m = glm::translate(glm::mat4(1.0f),
                                       glm::vec3(pos.x, pos.y - foot_offset_y * body_scale, pos.z));
    const glm::mat4 r = glm::rotate(m, yaw + glm::pi<float>(), glm::vec3(0.0f, 1.0f, 0.0f));
    return glm::scale(r, glm::vec3(body_scale));
}

void appendActorHurtboxes(const selva::anim::PoseSampler& sampler, const glm::mat4& actor_model,
                          const selva::gameplay::Body& body, OwnerRef owner,
                          selva::gameplay::Faction faction)
{
    if (sampler.jointCount() <= 0)
        return;
    const float base_r = body.collider_radius;
    auto& pool = hurtboxes();
    // Iterate declarations from the body. Authored per skeleton:
    // player loads config/skeletons/humanoid_male_hurtboxes.json; enemy
    // archetypes carry their own hurtboxes array. Empty = no
    // hurtboxes (actor takes no hits; intentional or misconfigured).
    pool.reserve(pool.size() + body.hurtbox_decls.size());
    // Once-per-actor diagnostic: log the first time we build hurtboxes
    // for each (owner kind, owner id) combo. Detects silent joint-name
    // misses (findJoint -> -1 collapses the capsule to actor origin,
    // and the player's swing-capsule sweeps over actor.pos which is at
    // the wolf's FEET -- nowhere near her body -- so hits land but
    // overlap nothing and the wolf takes no damage).
    static std::unordered_set<std::uint64_t> s_logged;
    const std::uint64_t key = (static_cast<std::uint64_t>(static_cast<int>(owner.kind)) << 32) |
                              static_cast<std::uint64_t>(static_cast<std::uint32_t>(owner.index));
    if (s_logged.insert(key).second)
    {
        std::fprintf(stderr, "[hurtbox-build] owner_kind=%d owner_index=%d decls=%zu base_r=%.3f\n",
                     static_cast<int>(owner.kind), owner.index, body.hurtbox_decls.size(), base_r);
        for (const auto& d : body.hurtbox_decls)
        {
            const int idx_a = sampler.findJoint(d.joint_a.c_str());
            const int idx_b = sampler.findJoint(d.joint_b.c_str());
            std::fprintf(stderr, "  '%s'->%d  '%s'->%d  region=%d radius_scale=%.2f%s\n",
                         d.joint_a.c_str(), idx_a, d.joint_b.c_str(), idx_b,
                         static_cast<int>(d.region), d.radius_scale,
                         (idx_a < 0 || idx_b < 0) ? "  <-- MISSING JOINT" : "");
        }
        std::fflush(stderr);
    }
    for (const auto& d : body.hurtbox_decls)
    {
        Hurtbox h = capsuleBetween(sampler, actor_model, d.joint_a.c_str(), d.joint_b.c_str(),
                                   d.region, base_r * d.radius_scale, owner, faction);
        h.damage_multiplier = d.damage_multiplier;
        pool.push_back(h);
    }
}

namespace
{
HurtRegion parseHurtRegion(const std::string& s)
{
    if (s == "Head")
        return HurtRegion::Head;
    if (s == "UpperLimb")
        return HurtRegion::UpperLimb;
    if (s == "LowerLimb")
        return HurtRegion::LowerLimb;
    return HurtRegion::Torso;
}
} // namespace

std::vector<HurtboxDecl> loadHurtboxDecls(const char* json_path)
{
    std::vector<HurtboxDecl> out;
    std::ifstream f(json_path);
    if (!f.is_open())
    {
        std::fprintf(stderr, "[hurtbox-load] cannot open %s\n", json_path);
        return out;
    }
    try
    {
        nlohmann::json doc;
        f >> doc;
        if (!doc.contains("hurtboxes") || !doc.at("hurtboxes").is_array())
        {
            std::fprintf(stderr, "[hurtbox-load] %s: missing 'hurtboxes' array\n", json_path);
            return out;
        }
        for (const auto& h : doc.at("hurtboxes"))
        {
            HurtboxDecl d;
            d.joint_a = h.value("joint_a", std::string{});
            d.joint_b = h.value("joint_b", std::string{});
            d.region = parseHurtRegion(h.value("region", std::string{"Torso"}));
            d.radius_scale = h.value("radius_scale", 0.5f);
            d.damage_multiplier = h.value("damage_multiplier", 1.0f);
            out.push_back(std::move(d));
        }
        std::fprintf(stderr, "[hurtbox-load] %s: %zu decls\n", json_path, out.size());
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "[hurtbox-load] %s parse failed: %s\n", json_path, e.what());
    }
    return out;
}

} // namespace selva::combat
