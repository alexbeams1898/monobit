#include "systems/PickupSystem.h"

#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "ecs/GameComponents.h"
#include "ecs/FeelConfig.h"
#include "renderers/NotificationRenderer.h"
#include "systems/PlayerSystem.h"

#include <vector>

#include <entt/entt.hpp>

namespace pickup
{
namespace
{
// Stepping distance: the foot box, roughly. Anything wider is a magnet wearing a costume.

// Purple-family tints by rarity, brightness by quality -- the reference's convention: the TYPE
// says how loudly it glows, the INSTANCE how brightly it came out.
SolidColor dropColor(const ItemDef& def, Quality q)
{
    const float base = 0.55f + 0.15f * static_cast<float>(q);
    switch (def.rarity)
    {
    case Rarity::Uncommon:
        return SolidColor{0.55f * base, 0.95f * base, 0.6f * base};
    case Rarity::Rare:
        return SolidColor{0.6f * base, 0.7f * base, 1.0f * base};
    case Rarity::Exceptional:
        return SolidColor{0.9f * base, 0.6f * base, 1.0f * base};
    case Rarity::Common:
    default:
        return SolidColor{0.85f * base, 0.82f * base, 0.7f * base};
    }
}
} // namespace

void spawnDrop(EntityManager& em, float x, float y, const ItemInstance& inst)
{
    auto& reg = em.registry();
    const entt::entity e = reg.create();
    Transform t{};
    t.x = x;
    t.y = y;
    reg.emplace<Transform>(e, t);
    reg.emplace<PreviousTransform>(e, PreviousTransform{x, y});
    reg.emplace<ItemDrop>(e, ItemDrop{inst});
    Sprite spr{};
    spr.src_w = 4;
    spr.src_h = 4;
    spr.layer = 2;
    reg.emplace<Sprite>(e, spr);
    reg.emplace<SolidColor>(e, dropColor(items::get(inst.item), inst.quality));
}

void update(EntityManager& em)
{
    auto& reg = em.registry();
    const entt::entity p = player::entity();
    if (!reg.valid(p))
        return;
    const auto& pt = reg.get<Transform>(p);
    auto& satchel = reg.get_or_emplace<Satchel>(p, Satchel{});

    std::vector<entt::entity> taken;
    for (auto [e, t, drop] : reg.view<Transform, ItemDrop>().each())
    {
        const float dx = pt.x - t.x;
        const float dy = pt.y - t.y;
        if (dx * dx + dy * dy > feel::current().reach.pickup * feel::current().reach.pickup)
            continue;

        // Stack by (item, quality): a fine flake and a crude flake stay different goods.
        bool stacked = false;
        for (auto& held : satchel.items)
            if (held.item == drop.contents.item && held.quality == drop.contents.quality)
            {
                held.count += drop.contents.count;
                stacked = true;
                break;
            }
        if (!stacked)
            satchel.items.push_back(drop.contents);

        const ItemDef& def = items::get(drop.contents.item);
        notify::item(drop.contents.item, def.name, drop.contents.count);
        taken.push_back(e);
    }
    for (const auto e : taken)
        reg.destroy(e);
}

} // namespace pickup
