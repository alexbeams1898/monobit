#pragma once

#include "FontManager.h"
#include "UIRenderer.h"

class TextureManager;
struct FormulaConfig;
struct ItemDef;
struct Stats;
struct Weapon;
enum class Rarity : uint8_t;

namespace ItemStatRenderer
{

// Must be called once at startup (from main.cpp) to enable icon rendering.
void init(TextureManager* tm);

// Draw an item's icon scaled to fit `size x size` pixels.
// No-op if def is null or has no icon_path.
void drawItemIcon(const ItemDef* def, float x, float y, float size);

// Format weight for display: "X.X lb".
std::string formatWeight(float weight);

// Map rarity tier to display color.
Color rarityColor(Rarity r);

// Map scaling float to letter grade (S/A/B/C/D/E or "-" for zero).
const char* scalingGrade(float scaling);

// Draw the weapon stat panel (name, damage, scaling, speed, weight, requirements).
// Returns the y position below the last line drawn.
// Set show_name=false to skip the name+separator header (e.g. when name is already shown).
// god_mode=true skips the stat-requirement penalty so the displayed bonus matches
// the damage actually applied in DamageSystem when the player has god mode on.
float renderWeaponStats(FontHandle body_font, const Weapon& w, const Stats& stats,
                        const FormulaConfig& f, const ItemDef* def, bool has_stats, float cx,
                        float y, float cw, float val_x, bool show_name = true,
                        bool god_mode = false);

// Convenience: build a Weapon from an ItemDef and render its stats.
float renderWeaponStatsFromDef(FontHandle body_font, const ItemDef& def, const Stats& stats,
                               const FormulaConfig& f, bool has_stats, float cx, float y, float cw,
                               float val_x, bool show_name = true, bool god_mode = false);

// Draw shield stat panel (name, guard, weight).
float renderShieldStats(FontHandle body_font, const ItemDef& def, float cx, float y, float cw,
                        float val_x, bool show_name = true);

// Draw armor stat panel (name, defense, weight).
float renderArmorStats(FontHandle body_font, const ItemDef& def, float cx, float y, float cw,
                       float val_x, bool show_name = true);

// Draw item stats for any category. Dispatches to weapon/shield/armor/generic as appropriate.
// For weapons, needs player stats and formulas. For others, just shows basic info.
float renderItemStats(FontHandle body_font, const ItemDef& def, const Stats& stats,
                      const FormulaConfig& f, bool has_stats, float cx, float y, float cw,
                      float val_x, bool show_name = true, bool god_mode = false);

// Maximum stat panel line count (for stable height reservation).
// Weapons have the most lines (name + sep + 5 stat rows + optional requirements = 7-8).
static constexpr int MAX_STAT_LINES = 8;

} // namespace ItemStatRenderer
