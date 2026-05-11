#include "combat/HitDetection.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <unordered_set>

namespace selva::combat
{

namespace
{

std::vector<HitEvent> sEvents;

// Once-per-swing memo. Key = (hitbox.id, target.kind, target.index).
// Hitbox IDs are unique per spawn, so a single swing's hitbox can't
// double-damage the same target across multiple frames.
struct MemoKey
{
    std::uint32_t hitbox_id;
    OwnerKind target_kind;
    int target_index;
    bool operator==(const MemoKey& o) const
    {
        return hitbox_id == o.hitbox_id && target_kind == o.target_kind &&
               target_index == o.target_index;
    }
};
struct MemoHash
{
    std::size_t operator()(const MemoKey& k) const noexcept
    {
        return std::hash<std::uint64_t>{}((std::uint64_t(k.hitbox_id) << 32) ^
                                          (std::uint64_t(k.target_index) << 1) ^
                                          std::uint64_t(k.target_kind));
    }
};
std::unordered_set<MemoKey, MemoHash> sHitMemo;

// Closest point on segment ab to point p, parameterized t in [0,1].
glm::vec3 closestPointOnSegment(const glm::vec3& a, const glm::vec3& b, const glm::vec3& p)
{
    const glm::vec3 ab = b - a;
    const float ab_len_sq = glm::dot(ab, ab);
    if (ab_len_sq <= 1e-8f)
        return a;
    const float t = std::clamp(glm::dot(p - a, ab) / ab_len_sq, 0.0f, 1.0f);
    return a + ab * t;
}

// Shortest distance between two line segments (p0..p1) and
// (q0..q1). Real-Time Collision Detection (Ericson) Chapter 5.
// Returns squared distance and the closest points c1, c2.
float segmentSegmentDistanceSq(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& q0,
                               const glm::vec3& q1, glm::vec3& c1, glm::vec3& c2)
{
    const glm::vec3 d1 = p1 - p0;
    const glm::vec3 d2 = q1 - q0;
    const glm::vec3 r = p0 - q0;
    const float a = glm::dot(d1, d1);
    const float e = glm::dot(d2, d2);
    const float f = glm::dot(d2, r);

    constexpr float kEps = 1e-8f;
    float s = 0.0f;
    float t = 0.0f;

    if (a <= kEps && e <= kEps)
    {
        c1 = p0;
        c2 = q0;
        return glm::dot(c1 - c2, c1 - c2);
    }
    if (a <= kEps)
    {
        t = std::clamp(f / e, 0.0f, 1.0f);
    }
    else
    {
        const float c = glm::dot(d1, r);
        if (e <= kEps)
        {
            s = std::clamp(-c / a, 0.0f, 1.0f);
        }
        else
        {
            const float b = glm::dot(d1, d2);
            const float denom = a * e - b * b;
            if (denom != 0.0f)
                s = std::clamp((b * f - c * e) / denom, 0.0f, 1.0f);
            t = (b * s + f) / e;
            if (t < 0.0f)
            {
                t = 0.0f;
                s = std::clamp(-c / a, 0.0f, 1.0f);
            }
            else if (t > 1.0f)
            {
                t = 1.0f;
                s = std::clamp((b - c) / a, 0.0f, 1.0f);
            }
        }
    }
    c1 = p0 + d1 * s;
    c2 = q0 + d2 * t;
    return glm::dot(c1 - c2, c1 - c2);
}

// Capsules overlap iff segment-segment distance <= summed radii.
bool capsulesOverlap(const Capsule& a, const Capsule& b, glm::vec3& contact_pos,
                     glm::vec3& contact_normal)
{
    glm::vec3 c1;
    glm::vec3 c2;
    const float dist_sq = segmentSegmentDistanceSq(a.p0, a.p1, b.p0, b.p1, c1, c2);
    const float sum_r = a.radius + b.radius;
    if (dist_sq > sum_r * sum_r)
        return false;
    contact_pos = (c1 + c2) * 0.5f;
    glm::vec3 dir = c2 - c1; // attacker -> target
    if (dir.y != 0.0f)
        dir.y = 0.0f; // xz-only normal for hit-reaction direction
    const float len = glm::length(dir);
    contact_normal = (len > 1e-6f) ? (dir / len) : glm::vec3(0.0f, 0.0f, 1.0f);
    return true;
}

} // namespace

void resetHitMemo()
{
    sHitMemo.clear();
}

const std::vector<HitEvent>& detectHits()
{
    sEvents.clear();
    for (const auto& hb : hitboxes())
    {
        if (hb.remaining_seconds <= 0.0f)
            continue;
        for (const auto& hu : hurtboxes())
        {
            if (hu.owner == hb.attacker)
                continue;
            if (!selva::gameplay::factionsHostile(hb.attacker_faction, hu.faction))
                continue;
            const MemoKey key{hb.id, hu.owner.kind, hu.owner.index};
            if (sHitMemo.find(key) != sHitMemo.end())
                continue;

            glm::vec3 contact_pos{};
            glm::vec3 contact_normal{};
            // Swept test: union the previous-frame and current-frame
            // hitbox segments by extending the attacker capsule from
            // prev.p0..p1 to current.p0..p1. Cheap approximation —
            // skips the tunnel between frames at low cost.
            Capsule swept = hb.shape;
            if (hb.has_prev)
            {
                swept.p0 = glm::min(hb.shape.p0, hb.prev_shape.p0);
                swept.p1 = glm::max(hb.shape.p1, hb.prev_shape.p1);
                swept.radius =
                    std::max(hb.shape.radius, hb.prev_shape.radius);
            }
            if (!capsulesOverlap(swept, hu.shape, contact_pos, contact_normal))
                continue;

            sHitMemo.insert(key);
            HitEvent ev;
            ev.attacker = hb.attacker;
            ev.target = hu.owner;
            ev.region = hu.region;
            ev.raw_damage = hb.raw_damage;
            ev.world_pos = contact_pos;
            ev.world_normal = contact_normal;
            sEvents.push_back(ev);
        }
    }
    return sEvents;
}

} // namespace selva::combat
