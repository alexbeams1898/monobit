#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

class SpriteCompositor;

// ---------------------------------------------------------------------------
// AppearanceOption -- one selectable item within a category (e.g. "Brown Long"
// hair, "Leather Vest" torso). file is the assembled-layer PNG path relative
// to the executable; empty string means "none" (no layer for this slot).
// ---------------------------------------------------------------------------
struct AppearanceOption
{
    std::string id;
    std::string label;
    std::string file;
    uint32_t swatch = 0; // packed RGBA for palette display (0 = no swatch)
};

// ---------------------------------------------------------------------------
// Category type: visual layer select vs numeric slider (e.g. size).
// ---------------------------------------------------------------------------
enum class AppearanceCategoryType : uint8_t
{
    Select, // choose one of N options; contributes a sprite layer
    Slider  // numeric value; does not contribute a layer
};

// ---------------------------------------------------------------------------
// AppearanceCategory -- a layer slot (body, hair, torso, legs, feet, head) or
// a numeric slider (size). Categories are ordered bottom-to-top for
// compositing (index 0 = back). path_prefix is prepended to option file names
// to form the full texture path.
// ---------------------------------------------------------------------------
struct AppearanceCategory
{
    std::string id;
    std::string label;
    bool required = false;
    bool hidden = false; // not shown in character creator; programmatically driven
    std::string path_prefix;
    std::vector<AppearanceOption> options;

    // If non-empty, this category's option is auto-resolved from another
    // category's selection (same option id). Not shown in character creator.
    // Example: head linked_to "body_color" -- picks the matching skin tone.
    std::string linked_to;

    // If non-empty, this category's file name is built by combining its own
    // selection with another category's selection: "{combine_with}_{this}.ext"
    // Example: hair_color combine_with "hair_style" -> "long_black.png"
    std::string combine_with;

    // Slider metadata (used only when type == Slider).
    AppearanceCategoryType type = AppearanceCategoryType::Select;
    float min_value = 0.0f;
    float max_value = 1.0f;
    float step_value = 0.01f;
    float default_value = 1.0f;
};

// ---------------------------------------------------------------------------
// AppearanceConfig -- the full layer manifest loaded from
// config/appearance/layers.json. Stored in registry ctx.
// ---------------------------------------------------------------------------
struct AppearanceConfig
{
    int frame_size = 64;
    std::vector<AppearanceCategory> categories;
    bool loaded = false;
    SpriteCompositor* compositor = nullptr; // set at startup, used by resolveAppearance
};

// ---------------------------------------------------------------------------
// AppearanceDef -- pending compositing data emplaced by ConfigLoader when an
// entity has an "appearance" block. resolveAppearance() reads this, calls the
// compositor, sets Sprite.texture_id, and removes the component.
//
// For enemies: layers is fully resolved (final file paths).
// For player: default_layers maps category_id -> option_id as a fallback;
// saved appearance from PlayerProfile overrides these.
// ---------------------------------------------------------------------------
struct AppearanceDef
{
    std::string sheet_path;
    std::vector<std::string> layers;
    std::unordered_map<std::string, std::string> default_layers;
    std::string layer_manifest;
    int sprite_layer = 2;
};
