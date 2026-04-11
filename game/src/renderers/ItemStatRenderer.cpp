#include "renderers/ItemStatRenderer.h"

#include "TextureManager.h"
#include "ecs/GameComponents.h"
#include "ecs/GameConfig.h"
#include "ecs/ItemConfig.h"
#include "systems/CombatSystem.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

static TextureManager* sTexMgr = nullptr;

void ItemStatRenderer::init(TextureManager* tm)
{
    sTexMgr = tm;
}

void ItemStatRenderer::drawItemIcon(const ItemDef* def, float x, float y, float size)
{
    if (def == nullptr || def->icon_path.empty() || sTexMgr == nullptr)
        return;
    const uint32_t tex = sTexMgr->load(def->icon_path);
    UIRenderer::drawTexturedRect(x, y, size, size, tex, 0.0f, 0.0f, 1.0f, 1.0f,
                                 {1.0f, 1.0f, 1.0f, 1.0f});
}

static constexpr Color TEXT_WHITE{0.92f, 0.90f, 0.88f, 1.0f};
static constexpr Color TEXT_DIM{0.5f, 0.48f, 0.46f, 1.0f};
static constexpr Color LABEL_COLOR{0.55f, 0.7f, 0.85f, 1.0f};
static constexpr Color SEP_COLOR{0.4f, 0.35f, 0.25f, 0.5f};
static constexpr Color REQ_MET{0.5f, 0.75f, 0.5f, 1.0f};
static constexpr Color REQ_UNMET{0.85f, 0.3f, 0.3f, 1.0f};

// Draw name + separator (shared by all item stat renderers).
static float drawNameHeader(FontHandle font, const ItemDef& def, float cx, float y, float cw)
{
    const float stat_line = FontManager::lineHeight(font) + 4.0f;
    UIRenderer::drawText(font, def.name, cx, y, ItemStatRenderer::rarityColor(def.rarity));
    y += stat_line;
    UIRenderer::drawRect(cx, y, cw, 1.0f, SEP_COLOR);
    y += 6.0f;
    return y;
}

namespace ItemStatRenderer
{

std::string formatWeight(float weight)
{
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%.1f lb", static_cast<double>(weight * 3.0f));
    return buf;
}

Color rarityColor(Rarity r)
{
    switch (r)
    {
    case Rarity::VeryCommon:
        return {0.45f, 0.45f, 0.42f, 1.0f};
    case Rarity::Common:
        return {0.75f, 0.73f, 0.70f, 1.0f};
    case Rarity::Uncommon:
        return {0.35f, 0.75f, 0.40f, 1.0f};
    case Rarity::Rare:
        return {0.35f, 0.55f, 0.95f, 1.0f};
    case Rarity::Epic:
        return {0.65f, 0.35f, 0.85f, 1.0f};
    case Rarity::Legendary:
        return {0.95f, 0.70f, 0.25f, 1.0f};
    }
    return TEXT_WHITE;
}

const char* scalingGrade(float scaling)
{
    if (scaling <= 0.0f)
        return "-";
    if (scaling >= 1.5f)
        return "S";
    if (scaling >= 1.0f)
        return "A";
    if (scaling >= 0.7f)
        return "B";
    if (scaling >= 0.4f)
        return "C";
    if (scaling >= 0.2f)
        return "D";
    return "E";
}

// Compute fully-resolved per-swing damage including stat-requirement penalty.
// Returns the weapon's base damage when there are no stats to scale against.
static float resolveTotalDamage(const Weapon& w, const Stats& stats, const FormulaConfig& f,
                                bool has_stats, bool god_mode)
{
    if (!has_stats)
        return w.base_damage;
    float total = computeDamage(w, stats, f);
    if (god_mode)
        return total;
    const int strDeficit = std::max(0, w.str_requirement - stats.str);
    const int dexDeficit = std::max(0, w.dex_requirement - stats.dex);
    if (strDeficit == 0 && dexDeficit == 0)
        return total;
    const float penalty =
        std::exp(-static_cast<float>(strDeficit) * f.stat_requirement.penalty_rate) *
        std::exp(-static_cast<float>(dexDeficit) * f.stat_requirement.penalty_rate);
    return total * penalty;
}

static const char* speedTierLabel(float speed)
{
    if (speed < 1.0f)
        return "Very Slow";
    if (speed < 1.5f)
        return "Slow";
    if (speed <= 2.5f)
        return "Normal";
    if (speed <= 4.0f)
        return "Fast";
    return "Very Fast";
}

// Build "STR N  DEX M" requirement text for whichever requirements are non-zero.
static std::string buildRequirementText(const Weapon& w)
{
    std::string req;
    if (w.str_requirement > 0)
        req += "STR " + std::to_string(w.str_requirement);
    if (w.dex_requirement > 0)
    {
        if (!req.empty())
            req += "  ";
        req += "DEX " + std::to_string(w.dex_requirement);
    }
    return req;
}

float renderWeaponStats(FontHandle body_font, const Weapon& w, const Stats& stats,
                        const FormulaConfig& f, const ItemDef* def, bool has_stats, float cx,
                        float y, float cw, float val_x, bool show_name, bool god_mode)
{
    const float stat_line = FontManager::lineHeight(body_font) + 4.0f;

    if (show_name)
    {
        const std::string name = (def != nullptr) ? def->name : "Unarmed";
        UIRenderer::drawText(body_font, name, cx, y, def ? rarityColor(def->rarity) : TEXT_DIM);
        y += stat_line;
        UIRenderer::drawRect(cx, y, cw, 1.0f, SEP_COLOR);
        y += 6.0f;
    }

    // Damage: base (+/-bonus). Bonus reflects fully-resolved per-swing damage
    // including stat-requirement penalty so under-stat heavy weapons show a
    // negative bonus instead of a misleading raw scaling number. DEF/armor are
    // target-side and stay out of the tooltip.
    const float total = resolveTotalDamage(w, stats, f, has_stats, god_mode);
    const int bonus = static_cast<int>(total) - static_cast<int>(w.base_damage);
    const std::string sign = bonus >= 0 ? "+" : "";
    UIRenderer::drawText(body_font, "Damage", cx, y, LABEL_COLOR);
    UIRenderer::drawText(body_font,
                         std::to_string(static_cast<int>(w.base_damage)) + " (" + sign +
                             std::to_string(bonus) + ")",
                         val_x, y, TEXT_WHITE);
    y += stat_line;

    // Scaling grades.
    UIRenderer::drawText(body_font, "Scaling", cx, y, LABEL_COLOR);
    UIRenderer::drawText(body_font,
                         "STR " + std::string(scalingGrade(w.str_scaling)) + "  DEX " +
                             std::string(scalingGrade(w.dex_scaling)),
                         val_x, y, TEXT_WHITE);
    y += stat_line;

    // Speed.
    const float cooldown = has_stats ? computeSwingCooldown(w, stats, f)
                                     : (f.swing.base_swing_time + w.weight * f.swing.weight_scale);
    const float speed = 1.0f / std::max(cooldown, 0.05f);
    char speed_buf[32];
    std::snprintf(speed_buf, sizeof(speed_buf), "%s (%.1f/s)", speedTierLabel(speed),
                  static_cast<double>(speed));
    UIRenderer::drawText(body_font, "Speed", cx, y, LABEL_COLOR);
    UIRenderer::drawText(body_font, speed_buf, val_x, y, TEXT_WHITE);
    y += stat_line;

    // Weight.
    UIRenderer::drawText(body_font, "Weight", cx, y, LABEL_COLOR);
    UIRenderer::drawText(body_font, formatWeight(w.weight), val_x, y, TEXT_WHITE);
    y += stat_line;

    // Requirements (only for real weapons, not fists).
    if (def != nullptr && (w.str_requirement > 0 || w.dex_requirement > 0))
    {
        const bool str_ok = stats.str >= w.str_requirement;
        const bool dex_ok = stats.dex >= w.dex_requirement;
        UIRenderer::drawText(body_font, "Requires", cx, y, LABEL_COLOR);
        UIRenderer::drawText(body_font, buildRequirementText(w), val_x, y,
                             (str_ok && dex_ok) ? REQ_MET : REQ_UNMET);
        y += stat_line;
    }

    return y;
}

float renderWeaponStatsFromDef(FontHandle body_font, const ItemDef& def, const Stats& stats,
                               const FormulaConfig& f, bool has_stats, float cx, float y, float cw,
                               float val_x, bool show_name, bool god_mode)
{
    Weapon w;
    w.name = def.name;
    w.weight = def.weight;
    w.base_damage = def.base_damage;
    w.str_scaling = def.str_scaling;
    w.dex_scaling = def.dex_scaling;
    w.str_requirement = def.str_requirement;
    w.dex_requirement = def.dex_requirement;
    return renderWeaponStats(body_font, w, stats, f, &def, has_stats, cx, y, cw, val_x, show_name,
                             god_mode);
}

float renderShieldStats(FontHandle body_font, const ItemDef& def, float cx, float y, float cw,
                        float val_x, bool show_name)
{
    const float stat_line = FontManager::lineHeight(body_font) + 4.0f;
    if (show_name)
        y = drawNameHeader(body_font, def, cx, y, cw);

    UIRenderer::drawText(body_font, "Guard", cx, y, LABEL_COLOR);
    UIRenderer::drawText(body_font, std::to_string(static_cast<int>(def.max_guard)), val_x, y,
                         TEXT_WHITE);
    y += stat_line;

    UIRenderer::drawText(body_font, "Weight", cx, y, LABEL_COLOR);
    UIRenderer::drawText(body_font, formatWeight(def.weight), val_x, y, TEXT_WHITE);
    y += stat_line;

    return y;
}

float renderArmorStats(FontHandle body_font, const ItemDef& def, float cx, float y, float cw,
                       float val_x, bool show_name)
{
    const float stat_line = FontManager::lineHeight(body_font) + 4.0f;
    if (show_name)
        y = drawNameHeader(body_font, def, cx, y, cw);

    UIRenderer::drawText(body_font, "Defense", cx, y, LABEL_COLOR);
    UIRenderer::drawText(body_font, "+" + std::to_string(static_cast<int>(def.defense_bonus)),
                         val_x, y, TEXT_WHITE);
    y += stat_line;

    UIRenderer::drawText(body_font, "Weight", cx, y, LABEL_COLOR);
    UIRenderer::drawText(body_font, formatWeight(def.weight), val_x, y, TEXT_WHITE);
    y += stat_line;

    return y;
}

float renderItemStats(FontHandle body_font, const ItemDef& def, const Stats& stats,
                      const FormulaConfig& f, bool has_stats, float cx, float y, float cw,
                      float val_x, bool show_name, bool god_mode)
{
    if (def.category == ItemCategory::Weapon)
        return renderWeaponStatsFromDef(body_font, def, stats, f, has_stats, cx, y, cw, val_x,
                                        show_name, god_mode);

    // Shields are armor with max_guard > 0.
    if (def.max_guard > 0.0f)
        return renderShieldStats(body_font, def, cx, y, cw, val_x, show_name);

    if (def.category == ItemCategory::Armor)
        return renderArmorStats(body_font, def, cx, y, cw, val_x, show_name);

    // Generic items: just name + weight.
    const float stat_line = FontManager::lineHeight(body_font) + 4.0f;
    if (show_name)
        y = drawNameHeader(body_font, def, cx, y, cw);

    UIRenderer::drawText(body_font, "Weight", cx, y, LABEL_COLOR);
    UIRenderer::drawText(body_font, formatWeight(def.weight), val_x, y, TEXT_WHITE);
    y += stat_line;

    return y;
}

} // namespace ItemStatRenderer
