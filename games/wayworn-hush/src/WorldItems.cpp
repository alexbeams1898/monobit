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
// What a floor item IS, as opposed to how it's drawn: bundled so this reads as one thing
// (a placed, takeable thing) rather than a run of loose positional arguments.
struct FloorItem
{
    std::string placement_id; // which placed thing (so taking it can be remembered)
    std::string icon;
    int size = 32;
    interaction::ActionKind action = interaction::ActionKind::None;
    std::string target; // item id (Pickup) or loot table id (Gather)
    float cx = 0.0f;
    float cy = 0.0f;
};

// Spawn one floor item: a Y-sorted world sprite (the icon) + an actionable-only
// interactable carrying `action`/`target`. Sorted by its base (its world-Y) so the player
// draws in front when below it and behind when above -- like every other world sprite.
void spawnFloorItem(EntityManager& em, const FloorItem& item, const Config& cfg)
{
    auto& reg = em.registry();
    const entt::entity e = reg.create();
    Transform t{};
    t.x = item.cx;
    t.y = item.cy;
    t.scale = cfg.scale;
    reg.emplace<Transform>(e, t);

    Sprite spr{};
    spr.texture_path = item.icon;
    spr.src_w = item.size;
    spr.src_h = item.size;
    spr.layer = 2; // character/prop layer -- Y-sorted against the player
    spr.use_sort_anchor = true;
    spr.sort_anchor = item.cy; // base = its world-Y (it sits on the ground at its center)
    reg.emplace<Sprite>(e, spr);

    interaction::Interactable inter{};
    inter.w = cfg.box;
    inter.h = cfg.box;
    inter.placement_id = item.placement_id; // so taking it can be remembered
    inter.action = item.action; // actionable-only: no observe_id -> interacting takes it directly
    inter.target = item.target;
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
    cfg.outline_r = j.value("outline_r", cfg.outline_r);
    cfg.outline_g = j.value("outline_g", cfg.outline_g);
    cfg.outline_b = j.value("outline_b", cfg.outline_b);
    cfg.outline_width = j.value("outline_width", cfg.outline_width);
    cfg.outline_alpha = j.value("outline_alpha", cfg.outline_alpha);
}

void updateOutlines(EntityManager& em, const Config& cfg)
{
    auto& reg = em.registry();
    for (auto [e, inter] : reg.view<interaction::Interactable>().each())
    {
        // Only actionable items get the rim cue (encounters use the glimmer). Active ->
        // outline on; not active -> off.
        if (inter.action == interaction::ActionKind::None)
            continue;
        if (inter.active)
            reg.emplace_or_replace<Outline>(e, Outline{cfg.outline_r, cfg.outline_g, cfg.outline_b,
                                                       cfg.outline_width, cfg.outline_alpha});
        else if (reg.all_of<Outline>(e))
            reg.remove<Outline>(e);
    }
}

void spawn(EntityManager& em, const std::vector<ldtk::PickupPlacement>& pickups,
           const inventory::Registry& items, const loot::Registry& loot, const Config& cfg,
           const std::unordered_set<std::string>& gone)
{
    for (const auto& p : pickups)
    {
        // Already taken on a previous visit: the map still says it's here, but this
        // pilgrim's walk says otherwise, and the walk wins.
        if (gone.count(p.placement_id) > 0)
            continue;

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
            spawnFloorItem(em,
                           FloorItem{p.placement_id, def->icon, cfg.gather_size,
                                     interaction::ActionKind::Pickup, p.target, p.cx, p.cy},
                           cfg);
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
            spawnFloorItem(em,
                           FloorItem{p.placement_id, cfg.gather_sprite, cfg.gather_size,
                                     interaction::ActionKind::Gather, p.target, p.cx, p.cy},
                           cfg);
        }
    }
}

} // namespace world_items
