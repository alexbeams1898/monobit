#include "screens/PauseMenu.h"

#include "CraftingOps.h"
#include "InventoryOps.h"
#include "UIRenderer.h"
#include "ecs/Components.h"
#include "ecs/GameComponents.h"
#include "ecs/GameConfig.h"
#include "systems/AudioSystem.h"
#include "systems/CombatSystem.h"
#include "systems/NotificationSystem.h"

#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <tracy/Tracy.hpp>
#include <vector>

static FontHandle sBodyFont = INVALID_FONT;
static FontHandle sTitleFont = INVALID_FONT;
static TextureManager* sTexMgr = nullptr;
static int sContentSel = 0;
static int sBottomSel = -1; // -1 = content, 0 = Resume, 1 = Quit
static bool sEquipPicking = false;
static int sEquipPickSel = 0;
static std::string sCraftMsg;
static float sCraftMsgTimer = 0.0f;
static Color sCraftMsgColor{};
static constexpr float CRAFT_MSG_DURATION = 2.0f;

// Dark gothic palette.
static constexpr Color OVERLAY{0.0f, 0.0f, 0.0f, 0.75f};
static constexpr Color PANEL_BG{0.06f, 0.06f, 0.09f, 0.92f};
static constexpr Color TAB_BG{0.10f, 0.10f, 0.14f, 0.9f};
static constexpr Color TAB_ACTIVE{0.22f, 0.20f, 0.35f, 1.0f};
static constexpr Color TAB_HOVER{0.16f, 0.15f, 0.24f, 1.0f};
static constexpr Color TITLE_COLOR{0.9f, 0.78f, 0.45f, 1.0f};
static constexpr Color TEXT_WHITE{0.92f, 0.90f, 0.88f, 1.0f};
static constexpr Color TEXT_DIM{0.5f, 0.48f, 0.46f, 1.0f};
static constexpr Color LABEL_COLOR{0.55f, 0.7f, 0.85f, 1.0f};
static constexpr Color SELECTED_BG{0.25f, 0.22f, 0.38f, 0.6f};
static constexpr Color SLOT_BG{0.12f, 0.12f, 0.14f, 0.8f};
static constexpr Color SLOT_SELECTED{0.28f, 0.25f, 0.42f, 0.9f};
static constexpr Color SLOT_BORDER{0.75f, 0.65f, 0.35f, 0.8f};
static constexpr Color SLOT_EMPTY{0.2f, 0.2f, 0.22f, 0.4f};
static constexpr Color HAVE_COLOR{0.4f, 0.8f, 0.45f, 1.0f};
static constexpr Color NEED_COLOR{0.85f, 0.35f, 0.35f, 1.0f};
static constexpr Color STAT_COLOR{0.65f, 0.75f, 0.9f, 1.0f};
static constexpr Color BTN_NORMAL{0.7f, 0.68f, 0.65f, 1.0f};
static constexpr Color BTN_RESUME_HL{0.95f, 0.88f, 0.55f, 1.0f};
static constexpr Color BTN_QUIT{0.75f, 0.3f, 0.3f, 1.0f};
static constexpr Color BTN_QUIT_HL{0.95f, 0.4f, 0.35f, 1.0f};
static constexpr Color BTN_BG{0.1f, 0.1f, 0.12f, 0.5f};
static constexpr Color BTN_BG_HL{0.18f, 0.16f, 0.25f, 0.7f};

static constexpr int GRID_COLS = 5;
static constexpr float SLOT_SIZE = 40.0f;
static constexpr float SLOT_GAP = 5.0f;

static const char* TAB_NAMES[UIState::TAB_COUNT] = {"Status", "Inventory", "Equipment", "Crafting"};

// O(1) hover hit-test for row-based lists. Returns hovered index or -1.
static int hoveredRow(float mx, float my, float cx, float cy, float cw, float row_h, int count)
{
    if (mx < cx - 4.0f || mx >= cx + cw + 4.0f || my < cy - 2.0f)
        return -1;
    int idx = static_cast<int>((my - (cy - 2.0f)) / row_h);
    return (idx >= 0 && idx < count) ? idx : -1;
}

// O(1) hover hit-test for a grid of slots. Returns hovered index or -1.
static int hoveredSlot(float mx, float my, float cx, float cy, int cols, int total)
{
    if (mx < cx || my < cy)
        return -1;
    int col = static_cast<int>((mx - cx) / (SLOT_SIZE + SLOT_GAP));
    int row = static_cast<int>((my - cy) / (SLOT_SIZE + SLOT_GAP));
    if (col < 0 || col >= cols)
        return -1;
    float lx = (mx - cx) - static_cast<float>(col) * (SLOT_SIZE + SLOT_GAP);
    float ly = (my - cy) - static_cast<float>(row) * (SLOT_SIZE + SLOT_GAP);
    if (lx > SLOT_SIZE || ly > SLOT_SIZE)
        return -1;
    int idx = row * cols + col;
    return (idx >= 0 && idx < total) ? idx : -1;
}

// Input helpers -- flatten nested for+if patterns for event checking.
static bool mouseClicked(const EntityManager& em, uint8_t button)
{
    for (uint8_t btn : em.mouse_down_events)
        if (btn == button)
            return true;
    return false;
}

static bool keyPressed(const EntityManager& em, int scancode)
{
    for (int key : em.key_down_events)
        if (key == scancode)
            return true;
    return false;
}

static bool confirmKeyPressed(const EntityManager& em)
{
    return keyPressed(em, SDL_SCANCODE_RETURN) || keyPressed(em, SDL_SCANCODE_KP_ENTER) ||
           keyPressed(em, SDL_SCANCODE_F);
}

static bool isMouseInRow(float mx, float my, float cx, float cw, float row_y, float line_h)
{
    return mx >= cx - 4.0f && mx < cx + cw + 4.0f && my >= row_y - 2.0f &&
           my < row_y - 2.0f + line_h;
}

// Draw text with word wrapping within max_width. Returns total height used.
static float drawTextWrapped(FontHandle font, const std::string& text, float x, float y,
                             float max_width, const Color& color)
{
    const float line_h = FontManager::lineHeight(font) + 2.0f;
    float total_h = 0.0f;
    std::string line;
    size_t i = 0;
    while (i <= text.size())
    {
        // Extract next word.
        size_t start = i;
        while (i < text.size() && text[i] != ' ')
            ++i;
        std::string word = text.substr(start, i - start);
        ++i; // skip space

        std::string candidate = line.empty() ? word : line + " " + word;
        TextSize csz = UIRenderer::measureText(font, candidate);
        if (csz.width > max_width && !line.empty())
        {
            UIRenderer::drawText(font, line, x, y + total_h, color);
            total_h += line_h;
            line = word;
        }
        else
        {
            line = candidate;
        }
    }
    if (!line.empty())
    {
        UIRenderer::drawText(font, line, x, y + total_h, color);
        total_h += line_h;
    }
    return total_h;
}

// Draw a section heading: title text (title font) + separator line. Returns y below.
static constexpr Color HEADING_COLOR{0.9f, 0.78f, 0.45f, 1.0f};
static constexpr Color HEADING_SEP{0.4f, 0.35f, 0.25f, 0.5f};

static float drawTabHeading(const char* text, float cx, float cy, float cw)
{
    TextSize sz = UIRenderer::measureText(sTitleFont, text);
    UIRenderer::drawText(sTitleFont, text, cx + (cw - sz.width) * 0.5f, cy, HEADING_COLOR);
    float y = cy + sz.height + 6.0f;
    UIRenderer::drawRect(cx, y, cw, 1.0f, HEADING_SEP);
    return y + 10.0f;
}

void PauseMenu::init(FontHandle body_font, FontHandle title_font, TextureManager* tm)
{
    sBodyFont = body_font;
    sTitleFont = title_font;
    sTexMgr = tm;
    sContentSel = 0;
    sBottomSel = -1;
}

void PauseMenu::reset()
{
    sContentSel = 0;
    sBottomSel = -1;
    sEquipPicking = false;
    sEquipPickSel = 0;
    sCraftMsgTimer = 0.0f;
}

static Color rarityColor(Rarity r)
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

static const char* scalingGrade(float scaling)
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

static int countItem(const Inventory& inv, const std::string& config_path)
{
    int total = 0;
    for (const auto& item : inv.items)
    {
        if (item.config_path == config_path)
            total += item.quantity;
    }
    return total;
}

static entt::entity findPlayer(EntityManager& em)
{
    for (auto e : em.registry().view<PlayerActions>())
        return e;
    return entt::null;
}

// ---------------------------------------------------------------------------
// Tab content renderers
// ---------------------------------------------------------------------------

static void renderStatusTab(EntityManager& em, float cx, float cy, float cw, float ch)
{
    entt::entity player = findPlayer(em);
    if (player == entt::null)
        return;

    const float line_h = FontManager::lineHeight(sBodyFont) + 4.0f;
    float y = drawTabHeading("Status", cx, cy, cw);

    const Experience* exp =
        em.registry().all_of<Experience>(player) ? &em.registry().get<Experience>(player) : nullptr;
    if (exp != nullptr)
    {
        UIRenderer::drawText(sBodyFont,
                             "Level " + std::to_string(exp->level) +
                                 "   XP: " + std::to_string(exp->current_xp) + "/" +
                                 std::to_string(exp->xp_to_next),
                             cx, y, TITLE_COLOR);
        y += line_h + 4.0f;
    }

    if (em.registry().all_of<Health>(player))
    {
        const auto& hp = em.registry().get<Health>(player);
        UIRenderer::drawText(sBodyFont,
                             "HP: " + std::to_string(hp.current) + " / " + std::to_string(hp.max),
                             cx, y, TEXT_WHITE);
        y += line_h;
    }

    if (em.registry().all_of<Stamina>(player))
    {
        const auto& sta = em.registry().get<Stamina>(player);
        const int pct = static_cast<int>((sta.current / sta.max_stamina) * 100.0f);
        UIRenderer::drawText(sBodyFont, "Stamina: " + std::to_string(pct) + "%", cx, y, TEXT_WHITE);
        y += line_h;
    }
    y += 8.0f;

    if (em.registry().all_of<Stats>(player))
    {
        const auto& s = em.registry().get<Stats>(player);
        const float col2 = cx + cw * 0.5f;

        UIRenderer::drawText(sBodyFont, "STR  " + std::to_string(s.str), cx, y, STAT_COLOR);
        UIRenderer::drawText(sBodyFont, "DEX  " + std::to_string(s.dex), col2, y, STAT_COLOR);
        y += line_h;
        UIRenderer::drawText(sBodyFont, "END  " + std::to_string(s.end), cx, y, STAT_COLOR);
        UIRenderer::drawText(sBodyFont, "LCK  " + std::to_string(s.lck), col2, y, STAT_COLOR);
        y += line_h + 4.0f;
    }

    if (em.registry().all_of<Wallet>(player))
    {
        const int money = em.registry().get<Wallet>(player).money;
        UIRenderer::drawText(sBodyFont, "Money: $" + std::to_string(money), cx, y,
                             {0.65f, 0.8f, 0.45f, 1.0f});
    }

    // Character portrait -- draw player body-part sprites at bottom-center.
    if (sTexMgr != nullptr)
    {
        constexpr float PORTRAIT_SCALE = 4.0f;
        struct PartDraw
        {
            uint32_t tex_id;
            int src_x, src_y, src_w, src_h;
            int tex_total_w, tex_total_h;
            int draw_order;
        };
        std::vector<PartDraw> parts;

        for (auto [child, bp, sprite] : em.registry().view<BodyPart, Sprite>().each())
        {
            if (bp.parent != player || sprite.texture_path.empty())
                continue;
            const uint32_t tex_id = sTexMgr->load(sprite.texture_path);
            if (tex_id == 0)
                continue;
            int tw = 0, th = 0;
            sTexMgr->getDimensions(sprite.texture_path, tw, th);
            if (tw > 0 && th > 0)
                parts.push_back({tex_id, sprite.src_x, sprite.src_y, sprite.src_w, sprite.src_h, tw,
                                 th, sprite.layer});
        }

        if (!parts.empty())
        {
            std::sort(parts.begin(), parts.end(), [](const PartDraw& a, const PartDraw& b)
                      { return a.draw_order < b.draw_order; });

            const float frame_w = static_cast<float>(parts[0].src_w) * PORTRAIT_SCALE;
            const float frame_h = static_cast<float>(parts[0].src_h) * PORTRAIT_SCALE;
            const float px = cx + (cw - frame_w) * 0.5f;
            const float py = cy + ch - frame_h - 8.0f;

            for (const auto& p : parts)
            {
                const float tw = static_cast<float>(p.tex_total_w);
                const float th = static_cast<float>(p.tex_total_h);
                const float u0 = static_cast<float>(p.src_x) / tw;
                const float v0 = static_cast<float>(p.src_y) / th;
                const float u1 = static_cast<float>(p.src_x + p.src_w) / tw;
                const float v1 = static_cast<float>(p.src_y + p.src_h) / th;
                UIRenderer::drawTexturedRect(px, py, frame_w, frame_h, p.tex_id, u0, v0, u1, v1);
            }
        }
    }
}

static void renderInventoryTab(EntityManager& em, float cx, float cy, float cw, float /*ch*/,
                               float mx, float my)
{
    entt::entity player = findPlayer(em);
    if (player == entt::null || !em.registry().all_of<Inventory>(player))
        return;

    float gy = drawTabHeading("Inventory", cx, cy, cw);

    const auto& inv = em.registry().get<Inventory>(player);
    const auto& items = em.registry().ctx().get<ItemRegistry>();
    const int total_slots = inv.max_slots;

    sContentSel = ((sContentSel % total_slots) + total_slots) % total_slots;

    int hover = hoveredSlot(mx, my, cx, gy, GRID_COLS, total_slots);
    if (hover >= 0)
    {
        sContentSel = hover;
        sBottomSel = -1;
    }

    for (int i = 0; i < total_slots; ++i)
    {
        const int col = i % GRID_COLS;
        const int row = i / GRID_COLS;
        const float sx = cx + static_cast<float>(col) * (SLOT_SIZE + SLOT_GAP);
        const float sy = gy + static_cast<float>(row) * (SLOT_SIZE + SLOT_GAP);
        const bool selected = (i == sContentSel);

        UIRenderer::drawRect(sx, sy, SLOT_SIZE, SLOT_SIZE, selected ? SLOT_SELECTED : SLOT_BG);

        // Selection border.
        if (selected)
        {
            const float b = 2.0f;
            UIRenderer::drawRect(sx, sy, SLOT_SIZE, b, SLOT_BORDER);
            UIRenderer::drawRect(sx, sy + SLOT_SIZE - b, SLOT_SIZE, b, SLOT_BORDER);
            UIRenderer::drawRect(sx, sy, b, SLOT_SIZE, SLOT_BORDER);
            UIRenderer::drawRect(sx + SLOT_SIZE - b, sy, b, SLOT_SIZE, SLOT_BORDER);
        }

        if (i < static_cast<int>(inv.items.size()) && !inv.items[static_cast<size_t>(i)].empty())
        {
            const auto& item = inv.items[static_cast<size_t>(i)];
            const ItemDef* def = items.find(item.config_path);
            const std::string letter =
                (def != nullptr && !def->name.empty()) ? def->name.substr(0, 1) : "?";
            const Rarity r = (def != nullptr) ? def->rarity : Rarity::Common;

            TextSize lsz = UIRenderer::measureText(sBodyFont, letter);
            UIRenderer::drawText(sBodyFont, letter, sx + (SLOT_SIZE - lsz.width) * 0.5f,
                                 sy + (SLOT_SIZE - lsz.height) * 0.5f, rarityColor(r));

            if (item.quantity > 1)
            {
                const std::string qty = std::to_string(item.quantity);
                TextSize qsz = UIRenderer::measureText(sBodyFont, qty);
                UIRenderer::drawText(sBodyFont, qty, sx + SLOT_SIZE - qsz.width - 3.0f,
                                     sy + SLOT_SIZE - qsz.height - 2.0f, TEXT_WHITE);
            }
        }
        else
        {
            UIRenderer::drawRect(sx + 3.0f, sy + 3.0f, SLOT_SIZE - 6.0f, SLOT_SIZE - 6.0f,
                                 SLOT_EMPTY);
        }
    }

    // Detail below the grid.
    const int rows = (total_slots + GRID_COLS - 1) / GRID_COLS;
    const float grid_bottom = gy + static_cast<float>(rows) * (SLOT_SIZE + SLOT_GAP) + 8.0f;

    if (sContentSel < static_cast<int>(inv.items.size()) &&
        !inv.items[static_cast<size_t>(sContentSel)].empty())
    {
        const auto& item = inv.items[static_cast<size_t>(sContentSel)];
        const ItemDef* def = items.find(item.config_path);

        if (def != nullptr)
        {
            UIRenderer::drawText(sBodyFont, def->name, cx, grid_bottom, rarityColor(def->rarity));
            const float desc_y = grid_bottom + FontManager::lineHeight(sBodyFont) + 2.0f;
            drawTextWrapped(sBodyFont, def->description, cx, desc_y, cw, TEXT_DIM);
        }
    }
}

// Equipment slot labels and corresponding EquipSlot enums (6 visible slots).
static const char* EQUIP_SLOT_NAMES[] = {"Weapon", "Off-hand", "Head", "Chest", "Legs", "Feet"};
static const EquipSlot EQUIP_SLOT_ENUMS[] = {EquipSlot::MainHand, EquipSlot::OffHand,
                                             EquipSlot::Head,     EquipSlot::Chest,
                                             EquipSlot::Legs,     EquipSlot::Feet};
static constexpr int EQUIP_SLOT_COUNT = 6;

struct PickerEntry
{
    int inv_index; // -1 = unequip/fists
    std::string label;
};

static std::vector<PickerEntry> buildPickerList(const Inventory& inv, const ItemRegistry& items,
                                                EquipSlot slot, bool slot_occupied)
{
    std::vector<PickerEntry> list;

    // First entry: unequip option if slot is occupied.
    if (slot_occupied)
    {
        const char* empty_label = (slot == EquipSlot::MainHand) ? "(Fists)" : "(Unequip)";
        list.push_back({-1, empty_label});
    }

    for (int i = 0; i < static_cast<int>(inv.items.size()); ++i)
    {
        const auto& item = inv.items[static_cast<size_t>(i)];
        if (item.empty())
            continue;

        const ItemDef* def = items.find(item.config_path);
        if (def == nullptr)
            continue;

        bool compatible = false;
        switch (slot)
        {
        case EquipSlot::MainHand:
            compatible = (def->category == ItemCategory::Weapon);
            break;
        case EquipSlot::OffHand:
            compatible = (def->category == ItemCategory::Armor && def->max_guard > 0.0f);
            break;
        case EquipSlot::Head:
            compatible = (def->category == ItemCategory::Armor && def->max_guard <= 0.0f &&
                          def->armor_slot == ArmorSlot::Head);
            break;
        case EquipSlot::Chest:
            compatible = (def->category == ItemCategory::Armor && def->max_guard <= 0.0f &&
                          def->armor_slot == ArmorSlot::Chest);
            break;
        case EquipSlot::Legs:
            compatible = (def->category == ItemCategory::Armor && def->max_guard <= 0.0f &&
                          def->armor_slot == ArmorSlot::Legs);
            break;
        case EquipSlot::Feet:
            compatible = (def->category == ItemCategory::Armor && def->max_guard <= 0.0f &&
                          def->armor_slot == ArmorSlot::Feet);
            break;
        default:
            break;
        }

        if (compatible)
        {
            std::string label = def->name;
            if (item.quantity > 1)
                label += " x" + std::to_string(item.quantity);
            list.push_back({i, label});
        }
    }

    return list;
}

// Shared stat-panel colors used by weapon/shield/armor renderers.
static constexpr Color SEP_COLOR{0.4f, 0.35f, 0.25f, 0.5f};
static constexpr Color REQ_MET{0.5f, 0.75f, 0.5f, 1.0f};
static constexpr Color REQ_UNMET{0.85f, 0.3f, 0.3f, 1.0f};

// Draw the weapon stat panel. Returns the y position below the last line drawn.
static float renderWeaponStats(const Weapon& w, const Stats& stats, const FormulaConfig& f,
                               const ItemDef* def, bool has_stats, float cx, float y, float cw,
                               float val_x)
{
    const float stat_line = FontManager::lineHeight(sBodyFont) + 4.0f;

    const std::string name = (def != nullptr) ? def->name : "(Fists)";
    UIRenderer::drawText(sBodyFont, name, cx, y, def ? rarityColor(def->rarity) : TEXT_DIM);
    y += stat_line;
    UIRenderer::drawRect(cx, y, cw, 1.0f, SEP_COLOR);
    y += 6.0f;

    // Damage: base (+bonus).
    const float total = has_stats ? computeDamage(w, stats, f) : w.base_damage;
    const int bonus = static_cast<int>(total) - static_cast<int>(w.base_damage);
    UIRenderer::drawText(sBodyFont, "Damage", cx, y, LABEL_COLOR);
    UIRenderer::drawText(sBodyFont,
                         std::to_string(static_cast<int>(w.base_damage)) + " (+" +
                             std::to_string(bonus) + ")",
                         val_x, y, TEXT_WHITE);
    y += stat_line;

    // Scaling grades.
    UIRenderer::drawText(sBodyFont, "Scaling", cx, y, LABEL_COLOR);
    UIRenderer::drawText(sBodyFont,
                         "STR " + std::string(scalingGrade(w.str_scaling)) + "  DEX " +
                             std::string(scalingGrade(w.dex_scaling)),
                         val_x, y, TEXT_WHITE);
    y += stat_line;

    // Speed.
    const float cooldown = has_stats ? computeSwingCooldown(w, stats, f)
                                     : (f.swing.base_swing_time + w.weight * f.swing.weight_scale);
    const float speed = 1.0f / std::max(cooldown, 0.05f);
    const char* speed_tier = nullptr;
    if (speed < 1.0f)
        speed_tier = "Very Slow";
    else if (speed < 1.5f)
        speed_tier = "Slow";
    else if (speed <= 2.5f)
        speed_tier = "Normal";
    else if (speed <= 4.0f)
        speed_tier = "Fast";
    else
        speed_tier = "Very Fast";
    char speed_buf[32];
    std::snprintf(speed_buf, sizeof(speed_buf), "%s (%.1f/s)", speed_tier, speed);
    UIRenderer::drawText(sBodyFont, "Speed", cx, y, LABEL_COLOR);
    UIRenderer::drawText(sBodyFont, speed_buf, val_x, y, TEXT_WHITE);
    y += stat_line;

    // Weight.
    UIRenderer::drawText(sBodyFont, "Weight", cx, y, LABEL_COLOR);
    UIRenderer::drawText(sBodyFont, std::to_string(static_cast<int>(w.weight * 3.0f)) + " lb",
                         val_x, y, TEXT_WHITE);
    y += stat_line;

    // Requirements (only for real weapons, not fists).
    if (def != nullptr && (w.str_requirement > 0 || w.dex_requirement > 0))
    {
        UIRenderer::drawText(sBodyFont, "Requires", cx, y, LABEL_COLOR);
        const bool str_ok = stats.str >= w.str_requirement;
        const bool dex_ok = stats.dex >= w.dex_requirement;
        std::string req;
        if (w.str_requirement > 0)
            req += "STR " + std::to_string(w.str_requirement);
        if (w.dex_requirement > 0)
        {
            if (!req.empty())
                req += "  ";
            req += "DEX " + std::to_string(w.dex_requirement);
        }
        UIRenderer::drawText(sBodyFont, req, val_x, y, (str_ok && dex_ok) ? REQ_MET : REQ_UNMET);
        y += stat_line;
    }

    return y;
}

// Draw the shield stat panel. Returns the y position below the last line drawn.
static float renderShieldStats(const ItemDef& def, EntityManager& em, entt::entity player, float cx,
                               float y, float cw, float val_x)
{
    const float stat_line = FontManager::lineHeight(sBodyFont) + 4.0f;

    UIRenderer::drawText(sBodyFont, def.name, cx, y, rarityColor(def.rarity));
    y += stat_line;
    UIRenderer::drawRect(cx, y, cw, 1.0f, SEP_COLOR);
    y += 6.0f;

    const bool has_shield = em.registry().all_of<Shield>(player);
    const float cur = has_shield ? em.registry().get<Shield>(player).guard_health : def.max_guard;
    UIRenderer::drawText(sBodyFont, "Guard", cx, y, LABEL_COLOR);
    UIRenderer::drawText(sBodyFont,
                         std::to_string(static_cast<int>(cur)) + "/" +
                             std::to_string(static_cast<int>(def.max_guard)),
                         val_x, y, TEXT_WHITE);
    y += stat_line;

    UIRenderer::drawText(sBodyFont, "Weight", cx, y, LABEL_COLOR);
    UIRenderer::drawText(sBodyFont, std::to_string(static_cast<int>(def.weight * 3.0f)) + " lb",
                         val_x, y, TEXT_WHITE);
    y += stat_line;

    return y;
}

// Draw the armor piece stat panel. Returns the y position below the last line drawn.
static float renderArmorStats(const ItemDef& def, float cx, float y, float cw, float val_x)
{
    const float stat_line = FontManager::lineHeight(sBodyFont) + 4.0f;

    UIRenderer::drawText(sBodyFont, def.name, cx, y, rarityColor(def.rarity));
    y += stat_line;
    UIRenderer::drawRect(cx, y, cw, 1.0f, SEP_COLOR);
    y += 6.0f;

    UIRenderer::drawText(sBodyFont, "Defense", cx, y, LABEL_COLOR);
    UIRenderer::drawText(sBodyFont, "+" + std::to_string(static_cast<int>(def.defense_bonus)),
                         val_x, y, TEXT_WHITE);
    y += stat_line;

    UIRenderer::drawText(sBodyFont, "Weight", cx, y, LABEL_COLOR);
    UIRenderer::drawText(sBodyFont, std::to_string(static_cast<int>(def.weight * 3.0f)) + " lb",
                         val_x, y, TEXT_WHITE);
    y += stat_line;

    return y;
}

// Perform equip or unequip based on a picker entry.
static void performEquipAction(EntityManager& em, entt::entity player, const PickerEntry& entry,
                               EquipSlot slot_enum, const ItemRegistry& items)
{
    auto& mut_inv = em.registry().get<Inventory>(player);
    auto& mut_eq = em.registry().get<Equipment>(player);
    if (entry.inv_index < 0)
        InventoryOps::unequipSlot(mut_inv, mut_eq, slot_enum);
    else
        InventoryOps::equipItem(mut_inv, mut_eq, entry.inv_index, items);
}

// Draw picker rows and handle mouse clicks on them.
static void drawPickerRows(EntityManager& em, entt::entity player,
                           const std::vector<PickerEntry>& picker, EquipSlot slot_enum,
                           const ItemRegistry& items, float cx, float y, float cw, float line_h,
                           float mx, float my)
{
    for (int i = 0; i < static_cast<int>(picker.size()); ++i)
    {
        const bool selected = (i == sEquipPickSel);
        if (selected)
            UIRenderer::drawRect(cx - 4.0f, y - 2.0f, cw + 8.0f, line_h, SELECTED_BG);

        const std::string prefix = selected ? "> " : "  ";
        const Color text_color =
            (picker[static_cast<size_t>(i)].inv_index < 0) ? TEXT_DIM : TEXT_WHITE;
        UIRenderer::drawText(sBodyFont, prefix + picker[static_cast<size_t>(i)].label, cx, y,
                             selected ? TEXT_WHITE : text_color);

        if (isMouseInRow(mx, my, cx, cw, y, line_h) && mouseClicked(em, SDL_BUTTON_LEFT))
        {
            sEquipPickSel = i;
            performEquipAction(em, player, picker[static_cast<size_t>(i)], slot_enum, items);
            sEquipPicking = false;
        }
        y += line_h;
    }
}

// Render the equipment picker (item selection for a slot). Returns true if right-click was
// consumed.
static bool renderEquipPicker(EntityManager& em, entt::entity player, const Equipment& eq,
                              const ItemRegistry& items, float cx, float ey, float cw, float mx,
                              float my)
{
    const float line_h = FontManager::lineHeight(sBodyFont) + 6.0f;
    const EquipSlot slot_enum = EQUIP_SLOT_ENUMS[sContentSel];
    const ItemInstance& current_slot = InventoryOps::slotRef(eq, slot_enum);
    const bool slot_occupied = !current_slot.empty();

    const Inventory* inv =
        em.registry().all_of<Inventory>(player) ? &em.registry().get<Inventory>(player) : nullptr;
    if (inv == nullptr)
    {
        sEquipPicking = false;
        return false;
    }

    auto picker = buildPickerList(*inv, items, slot_enum, slot_occupied);

    UIRenderer::drawText(sBodyFont,
                         std::string("Select for ") + EQUIP_SLOT_NAMES[sContentSel] + ":", cx, ey,
                         TITLE_COLOR);
    float y = ey + line_h + 4.0f;

    if (picker.empty())
    {
        UIRenderer::drawText(sBodyFont, "No compatible items.", cx, y, TEXT_DIM);
    }
    else
    {
        sEquipPickSel =
            ((sEquipPickSel % static_cast<int>(picker.size())) + static_cast<int>(picker.size())) %
            static_cast<int>(picker.size());

        int pickerHover = hoveredRow(mx, my, cx, y, cw, line_h, static_cast<int>(picker.size()));
        if (pickerHover >= 0)
            sEquipPickSel = pickerHover;

        drawPickerRows(em, player, picker, slot_enum, items, cx, y, cw, line_h, mx, my);

        if (confirmKeyPressed(em))
        {
            performEquipAction(em, player, picker[static_cast<size_t>(sEquipPickSel)], slot_enum,
                               items);
            sEquipPicking = false;
        }
    }

    bool rmbConsumed = mouseClicked(em, SDL_BUTTON_RIGHT);
    if (rmbConsumed)
        sEquipPicking = false;

    if (keyPressed(em, SDL_SCANCODE_UP) || keyPressed(em, SDL_SCANCODE_W))
        sEquipPickSel--;
    if (keyPressed(em, SDL_SCANCODE_DOWN) || keyPressed(em, SDL_SCANCODE_S))
        sEquipPickSel++;

    return rmbConsumed;
}

// Draw the equipment slot list and handle mouse clicks.
static void renderEquipSlotList(EntityManager& em, const Equipment& eq, const ItemRegistry& items,
                                float cx, float ey, float cw, float line_h, float mx, float my)
{
    float y = ey;
    for (int i = 0; i < EQUIP_SLOT_COUNT; ++i)
    {
        const bool selected = (sContentSel == i && sBottomSel < 0);
        const ItemInstance& slot = InventoryOps::slotRef(eq, EQUIP_SLOT_ENUMS[i]);

        if (selected)
            UIRenderer::drawRect(cx - 4.0f, y - 2.0f, cw + 8.0f, line_h, SELECTED_BG);

        std::string text = std::string(EQUIP_SLOT_NAMES[i]) + ": ";
        if (slot.empty())
        {
            text += (EQUIP_SLOT_ENUMS[i] == EquipSlot::MainHand) ? "(Fists)" : "(empty)";
            UIRenderer::drawText(sBodyFont, text, cx, y, selected ? TEXT_WHITE : TEXT_DIM);
        }
        else
        {
            const ItemDef* def = items.find(slot.config_path);
            const std::string name = (def != nullptr) ? def->name : "???";
            text += std::string(qualityName(slot.quality)) + " " + name;
            UIRenderer::drawText(sBodyFont, text, cx, y, TEXT_WHITE);
        }

        if (isMouseInRow(mx, my, cx, cw, y, line_h) && mouseClicked(em, SDL_BUTTON_LEFT))
        {
            sContentSel = i;
            sEquipPicking = true;
            sEquipPickSel = 0;
        }

        y += line_h;
    }
}

// Render the stat panel for the currently selected equipment slot.
static void renderEquipStatPanel(EntityManager& em, entt::entity player, const Equipment& eq,
                                 const ItemRegistry& items, float cx, float y, float cw)
{
    y += 12.0f;
    const EquipSlot sel_slot = EQUIP_SLOT_ENUMS[sContentSel];
    const ItemInstance& sel_item = InventoryOps::slotRef(eq, sel_slot);
    const ItemDef* def = sel_item.empty() ? nullptr : items.find(sel_item.config_path);
    const float val_x = cx + 120.0f;

    const bool has_stats = em.registry().all_of<Stats>(player);
    const Stats& stats = has_stats ? em.registry().get<Stats>(player) : Stats{1, 1, 1, 1};
    const auto& f = em.registry().ctx().get<FormulaConfig>();

    if (sel_slot == EquipSlot::MainHand)
    {
        const bool has_weapon = em.registry().all_of<Weapon>(player);
        const Weapon fist_w{"Fists", f.fist.weight,     f.fist.str_scaling, f.fist.dex_scaling, 0,
                            0,       f.fist.base_damage};
        const Weapon& w = has_weapon ? em.registry().get<Weapon>(player) : fist_w;
        renderWeaponStats(w, stats, f, def, has_stats, cx, y, cw, val_x);
    }
    else if (sel_slot == EquipSlot::OffHand && def != nullptr && def->max_guard > 0.0f)
    {
        renderShieldStats(*def, em, player, cx, y, cw, val_x);
    }
    else if (def != nullptr && def->category == ItemCategory::Armor)
    {
        renderArmorStats(*def, cx, y, cw, val_x);
    }
    else if (sel_item.empty())
    {
        const std::string empty_label = (sel_slot == EquipSlot::MainHand) ? "(Fists)" : "(empty)";
        UIRenderer::drawText(sBodyFont, empty_label, cx, y, TEXT_DIM);
    }
}

// Returns true if right-click was consumed (e.g. closing the picker).
static bool renderEquipmentTab(EntityManager& em, float cx, float cy, float cw, float mx, float my)
{
    entt::entity player = findPlayer(em);
    if (player == entt::null || !em.registry().all_of<Equipment>(player))
        return false;

    const auto& eq = em.registry().get<Equipment>(player);
    const auto& items = em.registry().ctx().get<ItemRegistry>();
    const float line_h = FontManager::lineHeight(sBodyFont) + 6.0f;
    float ey = drawTabHeading("Equipment", cx, cy, cw);

    if (sEquipPicking)
        return renderEquipPicker(em, player, eq, items, cx, ey, cw, mx, my);

    int hover = hoveredRow(mx, my, cx, ey, cw, line_h, EQUIP_SLOT_COUNT);
    if (hover >= 0)
    {
        sContentSel = hover;
        sBottomSel = -1;
    }

    renderEquipSlotList(em, eq, items, cx, ey, cw, line_h, mx, my);
    renderEquipStatPanel(em, player, eq, items, cx,
                         ey + line_h * static_cast<float>(EQUIP_SLOT_COUNT), cw);

    if (sBottomSel < 0 && confirmKeyPressed(em))
    {
        sEquipPicking = true;
        sEquipPickSel = 0;
    }

    return false;
}

// Select a color based on disabled/hovered state.
static const Color& selectBtnColor(bool disabled, bool hovered, const Color& normal,
                                   const Color& hover, const Color& dis)
{
    if (disabled)
        return dis;
    if (hovered)
        return hover;
    return normal;
}

static constexpr Color CRAFT_BTN_BG{0.12f, 0.18f, 0.14f, 0.8f};
static constexpr Color CRAFT_BTN_HL{0.18f, 0.28f, 0.22f, 0.9f};
static constexpr Color CRAFT_BTN_DISABLED_BG{0.10f, 0.10f, 0.10f, 0.6f};
static constexpr Color CRAFT_BTN_TXT{0.5f, 0.8f, 0.5f, 1.0f};
static constexpr Color CRAFT_BTN_TXT_HL{0.6f, 0.95f, 0.6f, 1.0f};
static constexpr Color CRAFT_BTN_TXT_DISABLED{0.35f, 0.35f, 0.35f, 0.6f};
static constexpr Color CRAFT_BTN_BORDER{0.4f, 0.7f, 0.45f, 0.6f};
static constexpr Color CRAFT_BTN_BORDER_HL{0.5f, 0.85f, 0.55f, 0.9f};
static constexpr Color CRAFT_BTN_BORDER_DISABLED{0.25f, 0.25f, 0.25f, 0.4f};

// Render the craft button, handle click/key, execute craft.
static void renderCraftButton(EntityManager& em, entt::entity player, const Inventory* inv,
                              const RecipeDef& recipe, const ItemRegistry& items, float cx,
                              float dy, float cw, float mx, float my)
{
    const bool canCraftNow = (inv != nullptr) && CraftingOps::canCraft(*inv, recipe, items);

    const std::string craft_label = "Craft [F]";
    TextSize craft_sz = UIRenderer::measureText(sTitleFont, craft_label);
    const float craft_pad_x = 24.0f;
    const float craft_pad_y = 8.0f;
    const float craft_bw = craft_sz.width + craft_pad_x * 2.0f;
    const float craft_bh = craft_sz.height + craft_pad_y * 2.0f;
    const float craft_bx = cx + (cw - craft_bw) * 0.5f;
    const float craft_by = dy;

    const bool craft_hover = canCraftNow && (mx >= craft_bx && mx < craft_bx + craft_bw &&
                                             my >= craft_by && my < craft_by + craft_bh);
    const bool disabled = !canCraftNow;

    UIRenderer::drawRect(
        craft_bx, craft_by, craft_bw, craft_bh,
        selectBtnColor(disabled, craft_hover, CRAFT_BTN_BG, CRAFT_BTN_HL, CRAFT_BTN_DISABLED_BG));
    const float cb = 1.5f;
    const Color& cbc = selectBtnColor(disabled, craft_hover, CRAFT_BTN_BORDER, CRAFT_BTN_BORDER_HL,
                                      CRAFT_BTN_BORDER_DISABLED);
    UIRenderer::drawRect(craft_bx, craft_by, craft_bw, cb, cbc);
    UIRenderer::drawRect(craft_bx, craft_by + craft_bh - cb, craft_bw, cb, cbc);
    UIRenderer::drawRect(craft_bx, craft_by, cb, craft_bh, cbc);
    UIRenderer::drawRect(craft_bx + craft_bw - cb, craft_by, cb, craft_bh, cbc);

    UIRenderer::drawText(sTitleFont, craft_label, craft_bx + craft_pad_x, craft_by + craft_pad_y,
                         selectBtnColor(disabled, craft_hover, CRAFT_BTN_TXT, CRAFT_BTN_TXT_HL,
                                        CRAFT_BTN_TXT_DISABLED));

    dy += craft_bh + 8.0f;

    bool tryCraft = (canCraftNow && craft_hover && mouseClicked(em, SDL_BUTTON_LEFT)) ||
                    (canCraftNow && sBottomSel < 0 && confirmKeyPressed(em));

    if (tryCraft && inv != nullptr && player != entt::null)
    {
        auto& mut_inv = em.registry().get<Inventory>(player);
        if (CraftingOps::craft(mut_inv, recipe, items))
        {
            TracyMessageL("ItemCrafted");
            const auto& snd = em.registry().ctx().get<SoundConfig>();
            AudioSystem::playSfx(snd.pickup.path, snd.pickup.volume);
            NotificationSystem::push("Crafted " + recipe.name + "!", {0.4f, 0.8f, 0.45f, 1.0f});
            sCraftMsg = "Crafted " + recipe.name + "!";
            sCraftMsgColor = HAVE_COLOR;
            sCraftMsgTimer = CRAFT_MSG_DURATION;
        }
        else
        {
            sCraftMsg = "Missing materials";
            sCraftMsgColor = NEED_COLOR;
            sCraftMsgTimer = CRAFT_MSG_DURATION;
        }
    }

    if (sCraftMsgTimer > 0.0f)
    {
        dy += 8.0f;
        const float alpha = std::min(1.0f, sCraftMsgTimer / 0.5f);
        Color fc = sCraftMsgColor;
        fc.a *= alpha;
        UIRenderer::drawText(sBodyFont, sCraftMsg, cx, dy, fc);
    }
}

// Render the detail panel for the selected recipe (result, requirements, craft button).
static void renderRecipeDetail(EntityManager& em, entt::entity player, const Inventory* inv,
                               const RecipeDef& recipe, const ItemRegistry& items, float cx,
                               float dy, float cw, float mx, float my)
{
    const float line_h = FontManager::lineHeight(sBodyFont) + 4.0f;

    const ItemDef* output_def = items.find(recipe.output_item);
    const std::string output_name = (output_def != nullptr) ? output_def->name : recipe.output_item;
    std::string result_str = "Result: " + output_name;
    if (recipe.output_quantity > 1)
        result_str += " x" + std::to_string(recipe.output_quantity);
    UIRenderer::drawText(sBodyFont, result_str, cx, dy, TEXT_WHITE);
    dy += line_h + 6.0f;

    UIRenderer::drawText(sBodyFont, "Requires:", cx, dy, TEXT_DIM);
    dy += line_h + 2.0f;

    for (const auto& ing : recipe.inputs)
    {
        const ItemDef* def = items.find(ing.config_path);
        const std::string name = (def != nullptr) ? def->name : ing.config_path;
        const int have = (inv != nullptr) ? countItem(*inv, ing.config_path) : 0;
        const Color c = (have >= ing.quantity) ? HAVE_COLOR : NEED_COLOR;
        UIRenderer::drawText(sBodyFont,
                             "  " + name + " " + std::to_string(have) + "/" +
                                 std::to_string(ing.quantity),
                             cx, dy, c);
        dy += line_h;
    }

    renderCraftButton(em, player, inv, recipe, items, cx, dy + 12.0f, cw, mx, my);
}

static void renderCraftingTab(EntityManager& em, float cx, float cy, float cw, float /*ch*/,
                              float mx, float my)
{
    const auto& recipes = em.registry().ctx().get<RecipeRegistry>();
    const auto& items = em.registry().ctx().get<ItemRegistry>();
    const int recipe_count = static_cast<int>(recipes.recipes.size());

    float gy = drawTabHeading("Crafting", cx, cy, cw);

    if (recipe_count == 0)
    {
        UIRenderer::drawText(sBodyFont, "No recipes available.", cx, gy, TEXT_DIM);
        return;
    }

    entt::entity player = findPlayer(em);
    const Inventory* inv = (player != entt::null && em.registry().all_of<Inventory>(player))
                               ? &em.registry().get<Inventory>(player)
                               : nullptr;

    sContentSel = ((sContentSel % recipe_count) + recipe_count) % recipe_count;

    float ry = gy;
    const float line_h = FontManager::lineHeight(sBodyFont) + 4.0f;

    int hover = hoveredRow(mx, my, cx, gy, cw, line_h, recipe_count);
    if (hover >= 0)
    {
        sContentSel = hover;
        sBottomSel = -1;
    }

    for (int i = 0; i < recipe_count; ++i)
    {
        const auto& recipe = recipes.recipes[static_cast<size_t>(i)];
        const bool selected = (i == sContentSel);

        if (selected)
            UIRenderer::drawRect(cx - 4.0f, ry - 2.0f, cw + 8.0f, line_h, SELECTED_BG);

        const std::string prefix = selected ? "> " : "  ";
        UIRenderer::drawText(sBodyFont, prefix + recipe.name, cx, ry,
                             selected ? TEXT_WHITE : TEXT_DIM);

        // Mouse click on recipe row selects it.
        if (mx >= cx - 4.0f && mx < cx + cw + 4.0f && my >= ry - 2.0f && my < ry - 2.0f + line_h)
        {
            for (uint8_t btn : em.mouse_down_events)
            {
                if (btn == SDL_BUTTON_LEFT)
                    sContentSel = i;
            }
        }

        ry += line_h;
    }

    // Detail below the recipe list.
    if (sContentSel < recipe_count)
    {
        const auto& recipe = recipes.recipes[static_cast<size_t>(sContentSel)];
        renderRecipeDetail(em, player, inv, recipe, items, cx, ry + 12.0f, cw, mx, my);
    }
}

// ---------------------------------------------------------------------------
// Helpers for PauseMenu::render
// ---------------------------------------------------------------------------

// Compute the max content index and whether the tab has navigable content.
static void computeMaxContentIndex(EntityManager& em, UIState::Tab menu_tab, int& maxContentIdx,
                                   bool& hasContent)
{
    maxContentIdx = 0;
    hasContent = false;
    switch (menu_tab)
    {
    case UIState::Tab::Status:
        break;
    case UIState::Tab::Equipment:
        if (!sEquipPicking)
        {
            maxContentIdx = EQUIP_SLOT_COUNT - 1;
            hasContent = true;
        }
        break;
    case UIState::Tab::Inventory:
    {
        entt::entity p = findPlayer(em);
        if (p != entt::null && em.registry().all_of<Inventory>(p))
        {
            maxContentIdx = em.registry().get<Inventory>(p).max_slots - 1;
            hasContent = true;
        }
        break;
    }
    case UIState::Tab::Crafting:
    {
        int rc = static_cast<int>(em.registry().ctx().get<RecipeRegistry>().recipes.size());
        if (rc > 0)
        {
            maxContentIdx = rc - 1;
            hasContent = true;
        }
        break;
    }
    }
}

// Handle up-arrow navigation in the pause menu.
static void handleNavUp(UIState::Tab menu_tab, bool hasContent)
{
    if (sBottomSel >= 0)
    {
        sBottomSel = -1;
        return;
    }
    if (!hasContent)
        return;
    if (menu_tab == UIState::Tab::Inventory)
        sContentSel -= GRID_COLS;
    else
        sContentSel--;
}

// Handle down-arrow navigation in the pause menu.
static void handleNavDown(UIState::Tab menu_tab, int maxContentIdx, bool hasContent)
{
    if (sBottomSel >= 0)
        return;
    if (!hasContent)
    {
        sBottomSel = 0;
        return;
    }
    const bool atBottom = (menu_tab == UIState::Tab::Inventory)
                              ? (sContentSel + GRID_COLS > maxContentIdx)
                              : (sContentSel >= maxContentIdx);
    if (atBottom)
    {
        sBottomSel = 0;
        return;
    }
    if (menu_tab == UIState::Tab::Inventory)
        sContentSel += GRID_COLS;
    else
        sContentSel++;
}

// Handle left/right arrow navigation.
static void handleNavHorizontal(int direction)
{
    if (sBottomSel >= 0)
        sBottomSel = (sBottomSel == 0) ? 1 : 0;
    else
        sContentSel += direction;
}

// Handle keyboard navigation in the pause menu. Returns true if quit was triggered.
static bool handleMenuKeyInput(EntityManager& em, UIState& ui, int tab, int maxContentIdx,
                               bool hasContent)
{
    if (keyPressed(em, SDL_SCANCODE_Q))
    {
        ui.menu_tab =
            static_cast<UIState::Tab>((tab - 1 + UIState::TAB_COUNT) % UIState::TAB_COUNT);
        sContentSel = 0;
        sBottomSel = -1;
        sEquipPicking = false;
    }
    if (keyPressed(em, SDL_SCANCODE_E))
    {
        ui.menu_tab = static_cast<UIState::Tab>((tab + 1) % UIState::TAB_COUNT);
        sContentSel = 0;
        sBottomSel = -1;
        sEquipPicking = false;
    }
    if (keyPressed(em, SDL_SCANCODE_UP) || keyPressed(em, SDL_SCANCODE_W))
        handleNavUp(ui.menu_tab, hasContent);
    if (keyPressed(em, SDL_SCANCODE_DOWN) || keyPressed(em, SDL_SCANCODE_S))
        handleNavDown(ui.menu_tab, maxContentIdx, hasContent);
    if (keyPressed(em, SDL_SCANCODE_LEFT) || keyPressed(em, SDL_SCANCODE_A))
        handleNavHorizontal(-1);
    if (keyPressed(em, SDL_SCANCODE_RIGHT) || keyPressed(em, SDL_SCANCODE_D))
        handleNavHorizontal(1);
    if (confirmKeyPressed(em))
    {
        if (sBottomSel == 0)
            ui.active_screen = UIState::Screen::None;
        else if (sBottomSel == 1)
            return true;
    }
    return false;
}

// Draw the tab bar at the top of the pause menu panel.
static void renderTabBar(EntityManager& em, UIState& ui, float panel_x, float panel_y,
                         float panel_w, float tab_h, float tab_w, float mx, float my)
{
    for (int i = 0; i < UIState::TAB_COUNT; ++i)
    {
        const float tx = panel_x + tab_w * static_cast<float>(i);
        const float ty = panel_y;
        const bool active = (i == static_cast<int>(ui.menu_tab));

        bool hovered = (mx >= tx && mx < tx + tab_w && my >= ty && my < ty + tab_h);
        if (hovered)
        {
            for (uint8_t btn : em.mouse_down_events)
            {
                if (btn == SDL_BUTTON_LEFT)
                {
                    ui.menu_tab = static_cast<UIState::Tab>(i);
                    sContentSel = 0;
                    sBottomSel = -1;
                }
            }
        }

        Color bg = active ? TAB_ACTIVE : (hovered ? TAB_HOVER : TAB_BG);
        UIRenderer::drawRect(tx, ty, tab_w, tab_h, bg);

        TextSize sz = UIRenderer::measureText(sBodyFont, TAB_NAMES[i]);
        UIRenderer::drawText(sBodyFont, TAB_NAMES[i], tx + (tab_w - sz.width) * 0.5f,
                             ty + (tab_h - sz.height) * 0.5f, active ? TEXT_WHITE : TEXT_DIM);
    }
}

// Draw the bottom bar (hint + Resume/Quit buttons + click handling). Returns true if quit.
static bool renderBottomBar(EntityManager& em, UIState& ui, float panel_x, float panel_w,
                            float bottom_area_y, float mx, float my, bool equipRmbConsumed)
{
    bool quit = false;
    const float line_h = FontManager::lineHeight(sBodyFont);

    // Controls hint (centered).
    const std::string hint = sEquipPicking ? "[W/S] Navigate   [F] Equip   [RMB] Back"
                                           : "[Q/E] Tab   [WASD] Navigate   [F] Select";
    TextSize hintSz = UIRenderer::measureText(sBodyFont, hint);
    UIRenderer::drawText(sBodyFont, hint, panel_x + (panel_w - hintSz.width) * 0.5f, bottom_area_y,
                         TEXT_DIM);

    // Resume / Quit buttons (centered, below hint).
    const float btn_y = bottom_area_y + line_h + 10.0f;
    const float btn_pad_x = 20.0f;
    const float btn_pad_y = 6.0f;
    const float btn_gap = 24.0f;

    const std::string resume_text = "Resume";
    const std::string quit_text = "Quit Game";
    TextSize rsz = UIRenderer::measureText(sBodyFont, resume_text);
    TextSize qsz = UIRenderer::measureText(sBodyFont, quit_text);

    const float rBtn_w = rsz.width + btn_pad_x * 2.0f;
    const float qBtn_w = qsz.width + btn_pad_x * 2.0f;
    const float btn_h = std::max(rsz.height, qsz.height) + btn_pad_y * 2.0f;
    const float total_btn_w = rBtn_w + btn_gap + qBtn_w;
    const float btn_start_x = panel_x + (panel_w - total_btn_w) * 0.5f;

    // Resume button.
    const float rBx = btn_start_x;
    bool rHover = (mx >= rBx && mx < rBx + rBtn_w && my >= btn_y && my < btn_y + btn_h);
    if (rHover)
        sBottomSel = 0;
    bool rSel = (sBottomSel == 0);
    UIRenderer::drawRect(rBx, btn_y, rBtn_w, btn_h, (rHover || rSel) ? BTN_BG_HL : BTN_BG);
    UIRenderer::drawText(sBodyFont, resume_text, rBx + btn_pad_x, btn_y + btn_pad_y,
                         (rHover || rSel) ? BTN_RESUME_HL : BTN_NORMAL);

    // Quit button.
    const float qBx = btn_start_x + rBtn_w + btn_gap;
    bool qHover = (mx >= qBx && mx < qBx + qBtn_w && my >= btn_y && my < btn_y + btn_h);
    if (qHover)
        sBottomSel = 1;
    bool qSel = (sBottomSel == 1);
    UIRenderer::drawRect(qBx, btn_y, qBtn_w, btn_h, (qHover || qSel) ? BTN_BG_HL : BTN_BG);
    UIRenderer::drawText(sBodyFont, quit_text, qBx + btn_pad_x, btn_y + btn_pad_y,
                         (qHover || qSel) ? BTN_QUIT_HL : BTN_QUIT);

    // Mouse click on buttons.
    for (uint8_t btn : em.mouse_down_events)
    {
        if (btn == SDL_BUTTON_LEFT)
        {
            if (rHover)
                ui.active_screen = UIState::Screen::None;
            if (qHover)
                quit = true;
        }
        if (btn == SDL_BUTTON_RIGHT && !equipRmbConsumed)
            ui.active_screen = UIState::Screen::None;
    }

    return quit;
}

// ---------------------------------------------------------------------------
// Main render
// ---------------------------------------------------------------------------

bool PauseMenu::render(EntityManager& em, int window_w, int window_h)
{
    ZoneScopedN("PauseMenu");

    const float ww = static_cast<float>(window_w);
    const float wh = static_cast<float>(window_h);
    auto& ui = em.registry().ctx().get<UIState>();
    const int tab = static_cast<int>(ui.menu_tab);
    bool quit = false;

    // Tick craft feedback timer.
    static uint32_t sLastTicks = 0;
    const uint32_t now = SDL_GetTicks();
    if (sLastTicks > 0 && sCraftMsgTimer > 0.0f)
        sCraftMsgTimer -= static_cast<float>(now - sLastTicks) / 1000.0f;
    sLastTicks = now;

    int maxContentIdx = 0;
    bool hasContent = false;
    computeMaxContentIndex(em, ui.menu_tab, maxContentIdx, hasContent);

    // Input (skipped when equipment picker is active -- picker handles its own keys).
    if (!sEquipPicking)
        quit = handleMenuKeyInput(em, ui, tab, maxContentIdx, hasContent);

    // --- Mouse state ---
    int mouseX = 0;
    int mouseY = 0;
    SDL_GetMouseState(&mouseX, &mouseY);
    const float mx = static_cast<float>(mouseX);
    const float my = static_cast<float>(mouseY);

    // --- Draw: overlay + panel ---
    UIRenderer::drawRect(0.0f, 0.0f, ww, wh, OVERLAY);

    const float panel_w = 520.0f;
    const float panel_h = 600.0f;
    const float panel_x = (ww - panel_w) * 0.5f;
    const float panel_y = (wh - panel_h) * 0.5f;
    UIRenderer::drawRect(panel_x, panel_y, panel_w, panel_h, PANEL_BG);

    // --- Draw: tab bar ---
    const float tab_h = FontManager::lineHeight(sBodyFont) + 12.0f;
    const float tab_w = panel_w / static_cast<float>(UIState::TAB_COUNT);
    renderTabBar(em, ui, panel_x, panel_y, panel_w, tab_h, tab_w, mx, my);

    // --- Draw: content area ---
    const float content_x = panel_x + 16.0f;
    const float content_y = panel_y + tab_h + 16.0f;
    const float content_w = panel_w - 32.0f;
    const float content_h = panel_h - tab_h - 16.0f - 90.0f;

    bool equipRmbConsumed = false;
    switch (ui.menu_tab)
    {
    case UIState::Tab::Status:
        renderStatusTab(em, content_x, content_y, content_w, content_h);
        break;
    case UIState::Tab::Inventory:
        renderInventoryTab(em, content_x, content_y, content_w, content_h, mx, my);
        break;
    case UIState::Tab::Equipment:
        equipRmbConsumed = renderEquipmentTab(em, content_x, content_y, content_w, mx, my);
        break;
    case UIState::Tab::Crafting:
        renderCraftingTab(em, content_x, content_y, content_w, content_h, mx, my);
        break;
    }

    // --- Draw: bottom bar ---
    const float bottom_area_y = panel_y + panel_h - 84.0f;
    quit =
        quit || renderBottomBar(em, ui, panel_x, panel_w, bottom_area_y, mx, my, equipRmbConsumed);

    return quit;
}
