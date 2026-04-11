#pragma once

#include "ecs/EntityManager.h"

#include <string>
#include <unordered_map>
#include <vector>

class SpriteCompositor;

namespace AppearanceOps
{

// Resolve an entity's AppearanceDef into a composited texture.
// For enemies: layers are already final paths in AppearanceDef.
// For player: overrides maps category_id -> option_id from SaveData;
//   falls back to AppearanceDef.default_layers, resolved via AppearanceConfig.
// Sets Sprite.texture_id, clears Sprite.texture_path, removes AppearanceDef.
void resolveAppearance(EntityManager& em, entt::entity entity, SpriteCompositor& compositor,
                       const std::unordered_map<std::string, std::string>& overrides = {});

// Convenience: pulls SpriteCompositor from AppearanceConfig in registry ctx.
// Use this from systems that don't have Engine access (e.g. WaveSystem).
void resolveAppearance(EntityManager& em, entt::entity entity,
                       const std::unordered_map<std::string, std::string>& overrides = {});

// Build the ordered layer path list from an appearance selection map.
// Uses AppearanceConfig from registry ctx to resolve category_id/option_id to file paths.
std::vector<std::string>
buildLayerPaths(EntityManager& em, const std::unordered_map<std::string, std::string>& selections);

// Parse a slider category's value from the selection map, clamping to the
// category's configured min/max. Returns 1.0f if the category is missing,
// not a slider, or the value fails to parse.
float resolveSliderValue(EntityManager& em, const std::string& categoryId,
                         const std::unordered_map<std::string, std::string>& selections);

// Apply the "size" slider to the entity's Transform.scale. Pure visual:
// does not touch collider, stats, or any gameplay values.
void applyAppearanceScale(EntityManager& em, entt::entity entity,
                          const std::unordered_map<std::string, std::string>& selections);

} // namespace AppearanceOps
