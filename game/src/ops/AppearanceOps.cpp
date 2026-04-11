#include "ops/AppearanceOps.h"

#include "SpriteCompositor.h"
#include "ecs/AppearanceConfig.h"
#include "ecs/Components.h"

#include <iostream>

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
buildLayerPaths(EntityManager& em, const std::unordered_map<std::string, std::string>& selections)
{
    const auto* cfg = em.registry().ctx().find<AppearanceConfig>();
    if (cfg == nullptr || !cfg->loaded)
        return {};

    std::vector<std::string> paths;
    for (const auto& cat : cfg->categories)
    {
        // Slider categories don't contribute a layer.
        if (cat.type == AppearanceCategoryType::Slider)
            continue;

        std::string optionId = resolveOptionId(cat, selections);
        if (optionId.empty() || optionId == "none")
        {
            paths.emplace_back();
            continue;
        }

        std::string file = resolveFileName(cat, optionId, selections);
        if (file.empty())
            paths.emplace_back();
        else
            paths.push_back(cat.path_prefix + file);
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
    if (layers.empty() && !def->layer_manifest.empty())
    {
        auto selections = def->default_layers;
        for (const auto& [k, v] : overrides)
            selections[k] = v;
        layers = buildLayerPaths(em, selections);
    }

    if (layers.empty())
    {
        std::cerr << "[AppearanceOps] No layers resolved for entity\n";
        reg.remove<AppearanceDef>(entity);
        return;
    }

    uint32_t texId = compositor.composite(layers);
    if (texId == 0)
    {
        std::cerr << "[AppearanceOps] Composite failed for entity\n";
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
