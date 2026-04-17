#include "ops/AppearanceOps.h"

#include "SpriteCompositor.h"
#include "ecs/AppearanceConfig.h"
#include "ecs/Components.h"
#include "ecs/GameComponents.h"

#include <iostream>

PaletteSwap PaletteRegistry::buildSwap(const std::string& palette_id,
                                       const std::string& base_color,
                                       const std::string& target_color) const
{
    PaletteSwap swap;
    if (base_color == target_color)
        return swap;
    const auto palIt = palettes.find(palette_id);
    if (palIt == palettes.end())
        return swap;
    const auto& pal = palIt->second;
    const auto baseIt = pal.find(base_color);
    const auto targetIt = pal.find(target_color);
    if (baseIt == pal.end() || targetIt == pal.end())
        return swap;
    const auto& base = baseIt->second;
    const auto& target = targetIt->second;
    const size_t count = std::min(base.size(), target.size());
    for (size_t i = 0; i < count; ++i)
    {
        swap.entries.push_back({base[i].r, base[i].g, base[i].b,
                                target[i].r, target[i].g, target[i].b});
    }
    return swap;
}

namespace AppearanceOps
{

static std::string resolveOptionId(const AppearanceCategory& cat,
                                   const std::unordered_map<std::string, std::string>& selections)
{
    // Linked categories copy the selection from another category.
    if (!cat.linked_to.empty())
    {
        auto linked = selections.find(cat.linked_to);
        return (linked != selections.end()) ? linked->second : std::string();
    }
    auto it = selections.find(cat.id);
    return (it != selections.end()) ? it->second : std::string();
}

static std::string resolveFileName(const AppearanceCategory& cat, const std::string& optionId,
                                   const std::unordered_map<std::string, std::string>& selections)
{
    // Weapon categories bypass options[] lookup -- the caller injects raw
    // filenames (e.g. "slingshot_behind.png") so equipped weapons don't need
    // to be enumerated in layers.json.
    if (cat.hidden)
    {
        auto it = selections.find(cat.id);
        if (it == selections.end() || it->second.empty() || it->second == "none")
            return {};
        return it->second;
    }

    // Compound file name: combine another category's selection with this one.
    // e.g. hair_color "black" + combine_with "hair_style" whose selection is "long"
    //      -> file = "long_black.png"
    // When the combined selection is "none", the layer is suppressed (empty path).
    // When the combined selection is "default", no prefix is added (just "{optionId}.png").
    if (!cat.combine_with.empty())
    {
        auto other = selections.find(cat.combine_with);
        if (other == selections.end() || other->second.empty() || other->second == "none")
            return {};
        if (other->second == "default")
            return optionId + ".png";
        return other->second + "_" + optionId + ".png";
    }

    // Normal: look up the option's file field.
    for (const auto& opt : cat.options)
    {
        if (opt.id == optionId)
            return opt.file;
    }
    return {};
}

std::vector<std::string>
buildLayerPaths(EntityManager& em, const std::unordered_map<std::string, std::string>& selections,
                std::vector<PaletteSwap>* out_palettes)
{
    const auto* cfg = em.registry().ctx().find<AppearanceConfig>();
    if (cfg == nullptr || !cfg->loaded)
        return {};

    const auto* palReg = em.registry().ctx().find<PaletteRegistry>();

    std::vector<std::string> paths;
    for (const auto& cat : cfg->categories)
    {
        if (cat.type == AppearanceCategoryType::Slider)
            continue;

        const std::string optionId = resolveOptionId(cat, selections);
        if (optionId.empty() || optionId == "none")
        {
            paths.emplace_back();
            if (out_palettes != nullptr)
                out_palettes->emplace_back();
            continue;
        }

        // If this category has a palette, use master path + palette swap.
        if (!cat.palette_id.empty() && !cat.base_color.empty() && palReg != nullptr)
        {
            std::string masterFile;
            std::string paletteTarget = optionId;

            if (cat.palette_from_combine && !cat.combine_with.empty())
            {
                // Master from option ID, palette target from combine_with.
                // Example: head_variant option="base", body_color="tone_2"
                //   -> master="base_master.png", palette target="tone_2"
                masterFile = optionId + "_master.png";
                auto other = selections.find(cat.combine_with);
                if (other != selections.end() && !other->second.empty())
                    paletteTarget = other->second;
                else
                    paletteTarget = cat.base_color;
            }
            else if (!cat.combine_with.empty())
            {
                // Master from combine_with selection, palette target from option.
                // Example: hair_color option="black", hair_style="long"
                //   -> master="long_master.png", palette target="black"
                auto other = selections.find(cat.combine_with);
                if (other != selections.end() && !other->second.empty() &&
                    other->second != "none")
                    masterFile = other->second + "_master.png";
            }
            else if (!cat.master_file.empty())
            {
                masterFile = cat.master_file;
            }

            if (masterFile.empty())
            {
                paths.emplace_back();
                if (out_palettes != nullptr)
                    out_palettes->emplace_back();
                continue;
            }

            paths.push_back(cat.path_prefix + masterFile);

            if (out_palettes != nullptr)
            {
                out_palettes->push_back(
                    palReg->buildSwap(cat.palette_id, cat.base_color, paletteTarget));
            }
        }
        else
        {
            const std::string file = resolveFileName(cat, optionId, selections);
            if (file.empty())
                paths.emplace_back();
            else
                paths.push_back(cat.path_prefix + file);
            if (out_palettes != nullptr)
                out_palettes->emplace_back();
        }
    }
    return paths;
}

float resolveSliderValue(EntityManager& em, const std::string& categoryId,
                         const std::unordered_map<std::string, std::string>& selections)
{
    const auto* cfg = em.registry().ctx().find<AppearanceConfig>();
    if (cfg == nullptr || !cfg->loaded)
        return 1.0f;

    const AppearanceCategory* cat = nullptr;
    for (const auto& c : cfg->categories)
    {
        if (c.id == categoryId && c.type == AppearanceCategoryType::Slider)
        {
            cat = &c;
            break;
        }
    }
    if (cat == nullptr)
        return 1.0f;

    float value = cat->default_value;
    auto it = selections.find(categoryId);
    if (it != selections.end() && !it->second.empty())
    {
        try
        {
            value = std::stof(it->second);
        }
        catch (const std::exception&)
        {
            value = cat->default_value;
        }
    }
    if (value < cat->min_value)
        value = cat->min_value;
    if (value > cat->max_value)
        value = cat->max_value;
    return value;
}

void applyAppearanceScale(EntityManager& em, entt::entity entity,
                          const std::unordered_map<std::string, std::string>& selections)
{
    auto& reg = em.registry();
    auto* t = reg.try_get<Transform>(entity);
    if (t == nullptr)
        return;
    t->scale = resolveSliderValue(em, "size", selections);
}

void resolveAppearance(EntityManager& em, entt::entity entity, SpriteCompositor& compositor,
                       const std::unordered_map<std::string, std::string>& overrides)
{
    auto& reg = em.registry();
    auto* def = reg.try_get<AppearanceDef>(entity);
    if (def == nullptr)
        return;

    std::vector<std::string> layers = def->layers;

    // If layers are empty, resolve from manifest + selections.
    std::vector<PaletteSwap> palettes;
    if (layers.empty() && !def->layer_manifest.empty())
    {
        auto selections = def->default_layers;
        for (const auto& [k, v] : overrides)
            selections[k] = v;
        layers = buildLayerPaths(em, selections, &palettes);
    }

    if (layers.empty())
    {
        std::cerr << "[AppearanceOps] No layers resolved for entity\n";
        reg.remove<AppearanceDef>(entity);
        return;
    }

    const uint32_t texId = compositor.composite(layers, palettes);
    if (texId == 0)
    {
        std::cerr << "[AppearanceOps] Composite failed for entity (missing layers)\n";
        // Hide the entity so it doesn't render as garbage.
        auto& spr = reg.get_or_emplace<Sprite>(entity);
        spr.src_w = 0;
        spr.src_h = 0;
        reg.remove<AppearanceDef>(entity);
        return;
    }

    auto& spr = reg.get_or_emplace<Sprite>(entity);
    spr.texture_id = texId;
    spr.texture_path.clear();
    spr.layer = def->sprite_layer;

    if (const auto* anim = reg.try_get<Animation>(entity))
    {
        spr.src_w = anim->frame_width;
        spr.src_h = anim->frame_height;
    }

    // Cache the resolved selections so AppearanceSyncSystem can rebuild
    // layer paths later when the equipped weapon changes.
    if (!def->layer_manifest.empty())
    {
        auto& state = reg.get_or_emplace<AppearanceState>(entity);
        auto merged = def->default_layers;
        for (const auto& [k, v] : overrides)
            merged[k] = v;
        state.current_selections = std::move(merged);
        state.synced_visual_weapon.clear();
    }

    reg.remove<AppearanceDef>(entity);
}

void resolveAppearance(EntityManager& em, entt::entity entity,
                       const std::unordered_map<std::string, std::string>& overrides)
{
    const auto* cfg = em.registry().ctx().find<AppearanceConfig>();
    if (cfg == nullptr || cfg->compositor == nullptr)
    {
        std::cerr << "[AppearanceOps] No AppearanceConfig or compositor in ctx\n";
        return;
    }
    resolveAppearance(em, entity, *cfg->compositor, overrides);
}

} // namespace AppearanceOps
