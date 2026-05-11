#include "combat/HitVolumes.h"

#include <algorithm>

namespace selva::combat
{

namespace
{
std::vector<Hurtbox> sHurtboxes;
std::vector<Hitbox> sHitboxes;
std::uint32_t sNextHitboxId = 1;
} // namespace

std::vector<Hurtbox>& hurtboxes()
{
    return sHurtboxes;
}

std::vector<Hitbox>& hitboxes()
{
    return sHitboxes;
}

void clearHurtboxes()
{
    sHurtboxes.clear();
}

void tickHitboxes(float dt)
{
    for (auto& hb : sHitboxes)
    {
        hb.prev_shape = hb.shape;
        hb.has_prev = true;
        hb.remaining_seconds -= dt;
    }
    sHitboxes.erase(std::remove_if(sHitboxes.begin(), sHitboxes.end(),
                                   [](const Hitbox& h) { return h.remaining_seconds <= 0.0f; }),
                    sHitboxes.end());
}

std::uint32_t spawnHitbox(const Hitbox& proto)
{
    Hitbox h = proto;
    h.id = sNextHitboxId++;
    h.prev_shape = h.shape;
    h.has_prev = false;
    sHitboxes.push_back(h);
    return h.id;
}

Hitbox* findHitbox(std::uint32_t id)
{
    for (auto& h : sHitboxes)
    {
        if (h.id == id)
            return &h;
    }
    return nullptr;
}

} // namespace selva::combat
