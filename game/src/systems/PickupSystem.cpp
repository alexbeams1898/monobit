#include "systems/PickupSystem.h"

#include "ecs/Components.h"
#include "ecs/GameComponents.h"
#include "ecs/GameConfig.h"
#include "ops/InventoryOps.h"
#include "systems/AudioSystem.h"
#include "systems/NotificationSystem.h"

#include <cmath>
#include <iostream>
#include <tracy/Tracy.hpp>
#include <vector>

// Max world-space distance from mouse cursor to highlight a pickup.
static constexpr float MOUSE_HOVER_RADIUS = 48.0f;
// Proximity radius for F-key and click collection range.
static constexpr float PROXIMITY_RADIUS = 72.0f;

// Play pickup SFX with pitch/volume scaled by item rarity and quality.
// Higher rarity + quality = higher pitch + louder + shimmer layer.
static void playPickupSfx(const SoundConfig& snd, Rarity rarity, QualityTier quality)
{
    // Rarity: 0 (VeryCommon) to 5 (Legendary). Quality: 0 (Crude) to 4 (Masterwork).
    const int rt = static_cast<int>(rarity);
    const int qt = static_cast<int>(quality);
    const float tier = static_cast<float>(rt) * 0.6f + static_cast<float>(qt) * 0.4f;

    // Pitch: 0.6 (common junk) -> 2.0 (legendary masterwork).
    const float pitch = 0.6f + tier * 0.28f;
    // Volume: base pickup volume scaled up for rarer items.
    const float vol = snd.pickup.volume * (1.0f + tier * 0.15f);

    AudioSystem::playSfx(snd.pickup.path, vol, pitch);

    // Shimmer layer for Rare+ items: a second voice at higher pitch, lower volume.
    if (rt >= static_cast<int>(Rarity::Rare))
        AudioSystem::playSfx(snd.pickup.path, vol * 0.5f, pitch * 1.6f);
}

static void collectPickup(EntityManager& em, entt::entity playerEnt, entt::entity pickupEnt)
{
    auto& reg = em.registry();
    const auto& pickup = reg.get<Pickup>(pickupEnt);
    const auto& items = reg.ctx().get<ItemRegistry>();
    const auto& snd = reg.ctx().get<SoundConfig>();

    // Money goes to Wallet.
    if (!pickup.item.empty())
    {
        const ItemDef* def = items.find(pickup.item.config_path);

        if (def != nullptr && def->category == ItemCategory::Money)
        {
            if (reg.all_of<Wallet>(playerEnt))
            {
                const int amount = def->value * pickup.item.quantity;
                reg.get<Wallet>(playerEnt).money += amount;
                reg.ctx().get<RunStats>().money += amount;
                AudioSystem::playSfx(snd.pickup.path, snd.pickup.volume);
                NotificationSystem::push("+$" + std::to_string(amount), {0.2f, 0.85f, 0.3f, 1.0f});
            }
            TracyMessageL("ItemPickedUp");
            em.destroy(pickupEnt);
            return;
        }

        // Item goes to inventory.
        if (!reg.all_of<Inventory>(playerEnt))
            return;
        auto& inv = reg.get<Inventory>(playerEnt);
        if (!InventoryOps::addItem(inv, pickup.item, items))
            return; // Inventory full -- leave pickup in world.

        // Discovery check: mark as newly discovered if first time picking up.
        auto& compendium = reg.ctx().get<Compendium>();
        const bool isNew = compendium.discover(pickup.item.config_path);
        if (isNew)
        {
            auto& last = inv.items.back();
            last.newly_discovered = true;
        }

        const std::string itemName = (def != nullptr) ? def->name : "item";
        const Rarity rarity = (def != nullptr) ? def->rarity : Rarity::Common;
        playPickupSfx(snd, rarity, pickup.item.quality);
        const std::string newTag = isNew ? " (NEW!)" : "";
        const Color notifColor =
            isNew ? Color{0.95f, 0.9f, 0.4f, 1.0f} : Color{0.8f, 0.8f, 0.8f, 1.0f};
        NotificationSystem::push("+" + std::to_string(pickup.item.quantity) + " " + itemName +
                                     " (" + rarityName(rarity) + ", " +
                                     qualityName(pickup.item.quality) + ")" + newTag,
                                 notifColor);
        TracyMessageL("ItemPickedUp");
        em.destroy(pickupEnt);
        return;
    }

    // XP pickup.
    if (pickup.xp_value > 0 && reg.all_of<Experience>(playerEnt))
    {
        auto& xp = reg.get<Experience>(playerEnt);
        xp.current_xp += pickup.xp_value;
        AudioSystem::playSfx(snd.pickup.path, snd.pickup.volume);
        NotificationSystem::push("+" + std::to_string(pickup.xp_value) + " XP",
                                 {0.3f, 0.5f, 1.0f, 1.0f});
    }
    em.destroy(pickupEnt);
}

// Find the nearest item pickup by mouse hover and player proximity.
static void findNearestPickup(entt::registry& reg, float mouseWX, float mouseWY, float playerX,
                              float playerY, entt::entity& hoverTarget, entt::entity& proxTarget)
{
    float hoverDist = MOUSE_HOVER_RADIUS;
    float proxDist = PROXIMITY_RADIUS;

    for (auto [entity, pickup, transform] : reg.view<Pickup, Transform>().each())
    {
        if (pickup.item.empty())
            continue;

        const float mdx = transform.x - mouseWX;
        const float mdy = transform.y - mouseWY;
        const float md = std::sqrt(mdx * mdx + mdy * mdy);
        if (md < hoverDist)
        {
            hoverDist = md;
            hoverTarget = entity;
        }

        const float pdx = transform.x - playerX;
        const float pdy = transform.y - playerY;
        const float pd = std::sqrt(pdx * pdx + pdy * pdy);
        if (pd < proxDist)
        {
            proxDist = pd;
            proxTarget = entity;
        }
    }
}

void PickupSystem::update(EntityManager& em)
{
    ZoneScopedN("PickupSystem");
    auto& reg = em.registry();

    entt::entity playerEnt = entt::null;
    float playerX = 0.0f;
    float playerY = 0.0f;
    float mouseWX = 0.0f;
    float mouseWY = 0.0f;
    bool wantInteract = false;
    bool wantClick = false;

    for (auto [entity, actions, transform] : reg.view<PlayerActions, Transform>().each())
    {
        playerEnt = entity;
        playerX = transform.x;
        playerY = transform.y;
        mouseWX = actions.mouse_world_x;
        mouseWY = actions.mouse_world_y;
        wantInteract = actions.interact;
        wantClick = actions.mouse_click;
        break;
    }

    if (playerEnt == entt::null)
        return;

    // XP pickups auto-collect.
    {
        std::vector<entt::entity> xpToCollect;
        for (auto [entity, pickup, transform] : reg.view<Pickup, Transform>().each())
        {
            if (pickup.xp_value <= 0 || !pickup.item.empty())
                continue;
            const float dx = transform.x - playerX;
            const float dy = transform.y - playerY;
            if (std::sqrt(dx * dx + dy * dy) <= pickup.radius)
                xpToCollect.push_back(entity);
        }
        for (auto e : xpToCollect)
            collectPickup(em, playerEnt, e);
    }

    // Item/money pickups: find best target then interact.
    entt::entity hoverTarget = entt::null;
    entt::entity proxTarget = entt::null;
    findNearestPickup(reg, mouseWX, mouseWY, playerX, playerY, hoverTarget, proxTarget);

    // Only consider targets within pickup radius of the player.
    entt::entity target = entt::null;
    auto checkRange = [&](entt::entity e) -> bool
    {
        if (e == entt::null || !reg.all_of<Transform>(e))
            return false;
        const auto& tt = reg.get<Transform>(e);
        const float dx = tt.x - playerX;
        const float dy = tt.y - playerY;
        return std::sqrt(dx * dx + dy * dy) <= PROXIMITY_RADIUS;
    };
    if (checkRange(hoverTarget))
        target = hoverTarget;
    else if (checkRange(proxTarget))
        target = proxTarget;

    if (reg.all_of<InteractTarget>(playerEnt))
    {
        const auto prevTarget = reg.get<InteractTarget>(playerEnt).entity;
        if (prevTarget != entt::null && prevTarget != target && reg.valid(prevTarget))
        {
            reg.remove<TintOverride>(prevTarget);
            if (reg.all_of<Transform>(prevTarget))
                reg.get<Transform>(prevTarget).scale = 1.0f;
        }
    }

    if (target != entt::null)
    {
        reg.emplace_or_replace<TintOverride>(target, TintOverride{2.0f, 2.0f, 2.0f});
        if (reg.all_of<Transform>(target))
            reg.get<Transform>(target).scale = 2.0f;
    }

    reg.get_or_emplace<InteractTarget>(playerEnt).entity = target;

    if (target != entt::null && (wantInteract || (wantClick && hoverTarget != entt::null)))
    {
        collectPickup(em, playerEnt, target);

        // Mark click as consumed so CombatSystem doesn't swing on it.
        if (wantClick)
            em.lmb_consumed = true;
    }
}
