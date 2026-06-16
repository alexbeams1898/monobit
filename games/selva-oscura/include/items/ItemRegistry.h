#pragma once

#include "ecs/Items.h"

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace selva::items
{

// Per-item gameplay extensions selva needs on top of the engine
// ItemDef. The engine layer owns the canonical item data
// (config_path, name, description, category, rarity, stackable,
// weapon stats, etc.); this struct holds the selva-specific glue
// that the engine intentionally doesn't know about. Keyed by the
// engine ItemDef's config_path.
//
// Today's extension surface:
//   - Use-button handler dispatch (UseHandlers.h)
//   - Per-class identity-stat scaling per
//     [[project_class_stats_v2_locked_2026_06_14]] -- weapons sing
//     in their aligned class's hands. The engine layer stays
//     class-agnostic; the identity-stat contribution + requirement
//     live here, in selva cosmology.
//
// Previously named `ItemHandlers`; renamed once selva-side fields
// grew past the use-button concern.
struct ItemExtensions
{
    // Use-button dispatch keys.
    std::string use_handler;   // C++ handler key registered via UseHandlers.h
    std::string use_condition; // C++ handler key for the Use-button gate

    // Class-identity-stat scaling. Empty `identity_class` means the
    // weapon has no identity contribution regardless of wielder.
    // Otherwise the wielder's class must match `identity_class` for
    // the identity stat to contribute to damage; off-class wielders
    // get the body-stat scaling only (and the soft-cap doctrine
    // already punishes them for it). Identity-class strings match
    // lowercase playerClassName() ("penitent" / "heretic" /
    // "ferine" / "unburdened").
    std::string identity_class;
    float identity_stat_scaling = 0.0f;
    int identity_stat_requirement = 0;
};

// Process-wide engine ItemRegistry. Single source of truth for
// item template data (engine::ecs::ItemDef). Loaded once at boot.
engine::ecs::ItemRegistry& itemRegistry();

// Side-table of selva-specific gameplay extensions keyed by
// engine ItemDef config_path. nullptr if no extensions for that
// item.
const ItemExtensions* itemExtensions(const std::string& config_path);

// Load every *.json in `dir` (recursive). Each file becomes one
// engine::ecs::ItemDef entry in itemRegistry() and -- when the
// JSON declares any selva-side extension keys (use_handler,
// use_condition, identity_class, etc.) -- an entry in the selva
// extensions side-table. Validates each item's category against
// the CategoryRegistry; items referencing an unknown category are
// logged and skipped.
void loadItemDirectory(const std::filesystem::path& dir);

// Process-wide engine RecipeRegistry. Loaded once at boot from
// config/recipes/*.json via engine::ecs::loadRecipeRegistry.
engine::ecs::RecipeRegistry& recipeRegistry();
void loadRecipeDirectory(const std::filesystem::path& dir);

// Selva-side craft entrypoint. Wraps engine::ops::crafting::craft()
// with the mastery-gated tier-unlock chain per
// [[project_healing_system_locked_2026_06_14]]:
//   1. Forward to engine craft. Returns false on engine failure.
//   2. On success: increment PlayerProfile.craft_counts[recipe.config_path].
//   3. If the recipe declares unlocks_recipe + unlock_after AND
//      the new count meets the threshold AND the unlocked recipe
//      isn't already known: append it to PlayerProfile.known_recipes.
//
// Use this anywhere the player crafts. Engine-side ops::crafting stays
// game-agnostic; this wrapper is where the unlock policy lives.
bool craftAndRecord(const engine::ecs::RecipeDef& recipe);

// True if `recipe_path` is in the active profile's known_recipes set.
// Used by the Craft tab to filter visible recipes. Defensive: returns
// false on null profile.
bool isRecipeKnown(const std::string& recipe_path);

// True if `def` is wearable in `slot`. Hand slots accept any
// weapon / incantation / invocation; armor slots match the def's
// armor_slot; accessory slots accept accessories. Engine layer stays
// agnostic about category-vs-slot policy; this predicate is selva's
// canonical version. Used by:
//   - Inventory equip popup (selva::ui)
//   - Combat hand-cycle hotkeys (selva::combat)
//   - Any future "what could go here" surface
bool itemFitsSlot(const engine::ecs::ItemDef& def, engine::ecs::EquipSlot slot);

// Every item instance in `inv` that fits `slot`. Pointers are stable
// for the lifetime of the inventory mutation; do NOT cache across
// frames if the inventory might mutate. Order is bucket-key
// (unordered_map iteration) -> insertion order within bucket.
std::vector<const engine::ecs::ItemInstance*>
collectItemsFittingSlot(const engine::ecs::Inventory& inv,
                        const engine::ecs::ItemRegistry& items,
                        engine::ecs::EquipSlot slot);

// Display name for an ItemInstance, with quality prefix for non-Common
// rolls. ONE source of truth for "what to call this item" across the
// inventory list, the detail panel title, the equip-slot popup, the
// combat HUD slots, and any future surface. Skips the prefix for
// Common quality so the most common roll doesn't pick up cosmetic
// noise ("Common Bark scrap" is redundant); above- and below-Common
// both prefix.
std::string itemDisplayName(const engine::ecs::ItemInstance& it,
                            const engine::ecs::ItemRegistry& items);

} // namespace selva::items
