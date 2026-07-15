#include "WorldItems.h"

#include "Interaction.h"
#include "JsonConfig.h"
#include "ecs/Components.h"
#include "ecs/EntityManager.h"

#include <SDL_log.h>

namespace world_items
{
namespace
{
// Spawn one floor item: a Y-sorted world sprite (the icon) + an actionable-only
// interactable carrying `action`/`target`. Sorted by its base (its world-Y) so the player
// draws in front when below it and behind when above -- like every other world sprite.
void spawnFloorItem(EntityManager& em, const std::string& icon, int size,
                    interaction::ActionKind action, const std::string& target, const Config& cfg,
                    float cx, float cy)
{
    auto& reg = em.registry();
    const entt::entity e = reg.create();
    Transform t{};
    t.x = cx;
    t.y = cy;
    t.scale = cfg.scale;
    reg.emplace<Transform>(e, t);

    Sprite spr{};
    spr.texture_path = icon;
    spr.src_w = size;
    spr.src_h = size;
    spr.layer = 2; // character/prop layer -- Y-sorted against the player
    spr.use_sort_anchor = true;
    spr.sort_anchor = cy; // base = the item's world-Y (it sits on the ground at its center)
    reg.emplace<Sprite>(e, spr);

    interaction::Interactable inter{};
    inter.w = cfg.box;
    inter.h = cfg.box;
    inter.action = action; // actionable-only: no observe_id -> interacting takes it directly
    inter.target = target;
    reg.emplace<interaction::Interactable>(e, inter);
}
} // namespace

void load(Config& cfg, const std::string& path)
{
    const auto loaded = config::load(path);
    if (!loaded)
        return;
    const nlohmann::json& j = *loaded;
    cfg.scale = j.value("scale", cfg.scale);
    cfg.box = j.value("box", cfg.box);
    cfg.gather_sprite = j.value("gather_sprite", cfg.gather_sprite);
    cfg.gather_size = j.value("gather_size", cfg.gather_size);
}

void spawn(EntityManager& em, const std::vector<ldtk::PickupPlacement>& pickups,
           const inventory::Registry& items, const loot::Registry& loot, const Config& cfg)
{
    for (const auto& p : pickups)
    {
        if (p.kind == ldtk::PickupPlacement::Kind::Item)
        {
            const inventory::ItemDef* def = items.find(p.target);
            if (!def)
            {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "pickup '%s' has no matching item def -- skipped", p.target.c_str());
                continue;
            }
            // The item's own icon IS its world cue -- one source of truth (the satchel shows
            // the same icon). Assume the icon is square at gather_size unless overridden.
            spawnFloorItem(em, def->icon, cfg.gather_size, interaction::ActionKind::Pickup,
                           p.target, cfg, p.cx, p.cy);
        }
        else // gather node: a stand-in sprite until node art is authored per spot
        {
            if (!loot.find(p.target))
            {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "gather node '%s' has no matching loot table -- skipped",
                            p.target.c_str());
                continue;
            }
            spawnFloorItem(em, cfg.gather_sprite, cfg.gather_size, interaction::ActionKind::Gather,
                           p.target, cfg, p.cx, p.cy);
        }
    }
}

} // namespace world_items
