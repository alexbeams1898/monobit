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
    // The art's source rect on `icon`. Square cells on a shared sheet are the common case
    // (col/row * size); a placement carrying its own tileset region says the rect outright,
    // which is how a thing gets to look like ITSELF rather than like what it yields.
    int size = 32;
    int col = -1; // <0 = the whole image, or `rect` if it is set
    int row = -1;
    bool has_rect = false;
    int rx = 0, ry = 0, rw = 0, rh = 0;
    float sort_offset = 0.0f; // depth base past the furniture it rests on
    interaction::ActionKind action = interaction::ActionKind::None;
    std::string target; // item id (a yield of exactly this) or yield table id (a roll)
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
    if (item.has_rect) // its own region of the world atlas (a pile of wood, not an icon)
    {
        spr.src_x = item.rx;
        spr.src_y = item.ry;
        spr.src_w = item.rw;
        spr.src_h = item.rh;
    }
    else if (item.col >= 0 && item.row >= 0) // a cell on the shared items sheet
    {
        spr.src_x = item.col * item.size;
        spr.src_y = item.row * item.size;
    }
    spr.layer = 2; // character/prop layer -- Y-sorted against the player
    spr.use_sort_anchor = true;
    // Base = its world-Y, plus any authored offset: a thing lying on the ground
    // sorts where it lies; a thing ON furniture sorts just past what holds it.
    spr.sort_anchor = item.cy + item.sort_offset;
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
           const inventory::Registry& items, const yields::Registry& tables, const Config& cfg,
           const std::unordered_set<std::string>& gone)
{
    for (const auto& p : pickups)
    {
        // Already taken on a previous visit: the map still says it's here, but this
        // pilgrim's walk says otherwise, and the walk wins.
        if (gone.count(p.placement_id) > 0)
            continue;

        // ONE placed thing; the yield is a field. What it gives decides which registry has to
        // know the target and which verb takes it -- nothing else about it differs.
        const bool draws = p.kind == ldtk::PickupPlacement::Kind::Table;
        const inventory::ItemDef* def = draws ? nullptr : items.find(p.target);
        if (draws ? tables.find(p.target) == nullptr : def == nullptr)
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "pickup '%s' has no matching %s -- skipped",
                        p.target.c_str(), draws ? "yield table" : "item def");
            continue;
        }

        FloorItem fi{};
        fi.placement_id = p.placement_id;
        fi.sort_offset = p.sort_offset;
        fi.action = draws ? interaction::ActionKind::Gather : interaction::ActionKind::Pickup;
        fi.target = p.target;
        fi.cx = p.cx;
        fi.cy = p.cy;
        if (p.sw > 0 && p.sh > 0)
        {
            // Its own art from the map's atlas -- the pile of storm wood IS the thing you
            // take, rather than a decorative prop with an invisible node sitting on it.
            fi.icon = p.texture_path;
            fi.has_rect = true;
            fi.rx = p.sx;
            fi.ry = p.sy;
            fi.rw = p.sw;
            fi.rh = p.sh;
        }
        else if (def != nullptr)
        {
            // The item's own icon IS its world cue -- one source of truth (the satchel shows
            // the same art). Its own cell size wins over the stand-in default.
            fi.icon = def->icon;
            fi.size = def->icon_size;
            fi.col = def->icon_col;
            fi.row = def->icon_row;
        }
        else
        {
            // A roll has no single icon and this one named no art: the configured stand-in.
            fi.icon = cfg.gather_sprite;
            fi.size = cfg.gather_size;
        }
        spawnFloorItem(em, fi, cfg);
    }
}

} // namespace world_items
