#include "screens/PauseMenu.h"

#include "UIRenderer.h"
#include "ecs/Components.h"
#include "ecs/GameComponents.h"
#include "ecs/GameConfig.h"
#include "ops/InventoryOps.h"
#include "renderers/ItemStatRenderer.h"
#include "screens/ScreenColors.h"
#include "screens/ScreenInput.h"
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

using screen_input::hoveredRow;
using screen_input::keyPressed;
using screen_input::mouseClicked;
using namespace screen_colors;

static FontHandle sBodyFont = INVALID_FONT;
static FontHandle sTitleFont = INVALID_FONT;
static TextureManager* sTexMgr = nullptr;
static int sContentSel = -1;
static int sBottomSel = -1; // -1 = content, 0 = Resume, 1 = Escape, 2 = Quit
static bool sEquipPicking = false;
static int sEquipPickSel = 0;
static constexpr Color PAUSE_BG{0.06f, 0.06f, 0.09f, 0.92f};
static constexpr Color TAB_BG{0.10f, 0.10f, 0.14f, 0.9f};
static constexpr Color TAB_ACTIVE{0.22f, 0.20f, 0.35f, 1.0f};
static constexpr Color TAB_HOVER{0.16f, 0.15f, 0.24f, 1.0f};
static constexpr Color TITLE_COLOR{0.9f, 0.78f, 0.45f, 1.0f};
static constexpr Color LABEL_COLOR{0.55f, 0.7f, 0.85f, 1.0f};
static constexpr Color SELECTED_BG{0.25f, 0.22f, 0.38f, 0.6f};
static constexpr Color SLOT_BG{0.12f, 0.12f, 0.14f, 0.8f};
static constexpr Color SLOT_SELECTED{0.28f, 0.25f, 0.42f, 0.9f};
static constexpr Color SLOT_BORDER{0.75f, 0.65f, 0.35f, 0.8f};
static constexpr Color SLOT_EMPTY{0.2f, 0.2f, 0.22f, 0.4f};
static constexpr Color STAT_COLOR{0.65f, 0.75f, 0.9f, 1.0f};
static constexpr Color BTN_RESUME_HL{0.95f, 0.88f, 0.55f, 1.0f};
static constexpr Color BTN_ESCAPE{0.3f, 0.65f, 0.85f, 1.0f};
static constexpr Color BTN_ESCAPE_HL{0.4f, 0.8f, 0.95f, 1.0f};
static constexpr Color BTN_QUIT{0.75f, 0.3f, 0.3f, 1.0f};
static constexpr Color BTN_QUIT_HL{0.95f, 0.4f, 0.35f, 1.0f};

static constexpr int GRID_COLS = 5;
static constexpr float SLOT_SIZE = 40.0f;
static constexpr float SLOT_GAP = 5.0f;

static const char* TAB_NAMES[UIState::TAB_COUNT] = {"Status", "Inventory", "Equipment"};

// O(1) hover hit-test for a grid of slots. Returns hovered index or -1.
static int hoveredSlot(float mx, float my, float cx, float cy, int cols, int total)
{
    if (mx < cx || my < cy)
        return -1;
    const int col = static_cast<int>((mx - cx) / (SLOT_SIZE + SLOT_GAP));
    const int row = static_cast<int>((my - cy) / (SLOT_SIZE + SLOT_GAP));
    if (col < 0 || col >= cols)
        return -1;
    const float lx = (mx - cx) - static_cast<float>(col) * (SLOT_SIZE + SLOT_GAP);
    const float ly = (my - cy) - static_cast<float>(row) * (SLOT_SIZE + SLOT_GAP);
    if (lx > SLOT_SIZE || ly > SLOT_SIZE)
        return -1;
    const int idx = row * cols + col;
    return (idx >= 0 && idx < total) ? idx : -1;
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
        const size_t start = i;
        while (i < text.size() && text[i] != ' ')
            ++i;
        const std::string word = text.substr(start, i - start);
        ++i; // skip space

        std::string candidate;
        if (line.empty())
        {
            candidate = word;
        }
        else
        {
            candidate = line;
            candidate += " ";
            candidate += word;
        }
        const TextSize csz = UIRenderer::measureText(font, candidate);
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
    const TextSize sz = UIRenderer::measureText(sTitleFont, text);
    UIRenderer::drawText(sTitleFont, text, cx + (cw - sz.width) * 0.5f, cy, HEADING_COLOR);
    const float y = cy + sz.height + 6.0f;
    UIRenderer::drawRect(cx, y, cw, 1.0f, HEADING_SEP);
    return y + 10.0f;
}

void PauseMenu::init(FontHandle body_font, FontHandle title_font, TextureManager* tm)
{
    sBodyFont = body_font;
    sTitleFont = title_font;
    sTexMgr = tm;
    sContentSel = -1;
    sBottomSel = -1;
}

void PauseMenu::reset()
{
    sContentSel = -1;
    sBottomSel = -1;
    sEquipPicking = false;
    sEquipPickSel = 0;
}

// rarityColor and scalingGrade are in ItemStatRenderer.

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
    const entt::entity player = findPlayer(em);
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

    // Character portrait -- draw the player's current sprite at bottom-center.
    if (sTexMgr != nullptr && em.registry().all_of<Sprite>(player))
    {
        constexpr float PORTRAIT_SCALE = 4.0f;
        const auto& sprite = em.registry().get<Sprite>(player);
        if (!sprite.texture_path.empty())
        {
            const uint32_t tex_id = sTexMgr->load(sprite.texture_path);
            int tw = 0, th = 0;
            sTexMgr->getDimensions(sprite.texture_path, tw, th);
            if (tex_id != 0 && tw > 0 && th > 0)
            {
                const float frame_w = static_cast<float>(sprite.src_w) * PORTRAIT_SCALE;
                const float frame_h = static_cast<float>(sprite.src_h) * PORTRAIT_SCALE;
                const float px = cx + (cw - frame_w) * 0.5f;
                const float py = cy + ch - frame_h - 8.0f;
                const float u0 = static_cast<float>(sprite.src_x) / static_cast<float>(tw);
                const float v0 = static_cast<float>(sprite.src_y) / static_cast<float>(th);
                const float u1 =
                    static_cast<float>(sprite.src_x + sprite.src_w) / static_cast<float>(tw);
                const float v1 =
                    static_cast<float>(sprite.src_y + sprite.src_h) / static_cast<float>(th);
                UIRenderer::drawTexturedRect(px, py, frame_w, frame_h, tex_id, u0, v0, u1, v1);
            }
        }
    }
}

static void renderInventoryTab(EntityManager& em, float cx, float cy, float cw, float /*ch*/,
                               float mx, float my)
{
    const entt::entity player = findPlayer(em);
    if (player == entt::null || !em.registry().all_of<Inventory>(player))
        return;

    const float gy = drawTabHeading("Inventory", cx, cy, cw);

    const auto& inv = em.registry().get<Inventory>(player);
    const auto& items = em.registry().ctx().get<ItemRegistry>();
    const int total_slots = inv.max_slots;

    if (sContentSel >= 0)
        sContentSel = ((sContentSel % total_slots) + total_slots) % total_slots;

    const int hover = hoveredSlot(mx, my, cx, gy, GRID_COLS, total_slots);
    if (hover >= 0 && mouseClicked(em, SDL_BUTTON_LEFT))
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
        const bool hovered = (i == hover && !selected);

        Color slotBg = SLOT_BG;
        if (selected)
            slotBg = SLOT_SELECTED;
        else if (hovered)
            slotBg = HOVERED_BG;
        UIRenderer::drawRect(sx, sy, SLOT_SIZE, SLOT_SIZE, slotBg);

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
            const float icon_pad = 3.0f;
            ItemStatRenderer::drawItemIcon(def, sx + icon_pad, sy + icon_pad,
                                           SLOT_SIZE - icon_pad * 2.0f);

            if (item.quantity > 1)
            {
                const std::string qty = std::to_string(item.quantity);
                const TextSize qsz = UIRenderer::measureText(sBodyFont, qty);
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

    const int displaySlot = (hover >= 0) ? hover : sContentSel;
    if (displaySlot >= 0 && displaySlot < static_cast<int>(inv.items.size()) &&
        !inv.items[static_cast<size_t>(displaySlot)].empty())
    {
        const auto& item = inv.items[static_cast<size_t>(displaySlot)];
        const ItemDef* def = items.find(item.config_path);

        if (def != nullptr)
        {
            UIRenderer::drawText(sBodyFont, def->name, cx, grid_bottom,
                                 ItemStatRenderer::rarityColor(def->rarity));
            const float desc_y = grid_bottom + FontManager::lineHeight(sBodyFont) + 2.0f;
            drawTextWrapped(sBodyFont, def->description, cx, desc_y, cw, TEXT_DIM);
        }
    }
}

// Equipment slot labels and corresponding EquipSlot enums (6 visible slots).
static const char* EQUIP_SLOT_NAMES[] = {"Right Hand", "Left Hand", "Head", "Chest", "Legs", "Feet"};
static const EquipSlot EQUIP_SLOT_ENUMS[] = {EquipSlot::RightHand, EquipSlot::LeftHand,
                                             EquipSlot::Head,     EquipSlot::Chest,
                                             EquipSlot::Legs,     EquipSlot::Feet};
static constexpr int EQUIP_SLOT_COUNT = 6;

struct PickerEntry
{
    int inv_index; // -1 = unequip/fists
    std::string label;
    std::string config_path;
};

static std::vector<PickerEntry> buildPickerList(const Inventory& inv, const ItemRegistry& items,
                                                EquipSlot slot, bool slot_occupied)
{
    std::vector<PickerEntry> list;

    // First entry: unequip option if slot is occupied.
    if (slot_occupied)
    {
        const char* empty_label = (slot == EquipSlot::RightHand) ? "(Unarmed)" : "(Unequip)";
        list.push_back({-1, empty_label, {}});
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
        case EquipSlot::RightHand:
        case EquipSlot::LeftHand:
            compatible = (def->category == ItemCategory::Weapon) ||
                         (def->category == ItemCategory::Armor && def->max_guard > 0.0f);
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
            list.push_back({i, label, item.config_path});
        }
    }

    return list;
}

// Shared stat-panel colors used by shield/armor renderers.
static constexpr Color SEP_COLOR{0.4f, 0.35f, 0.25f, 0.5f};

// Draw the shield stat panel. Returns the y position below the last line drawn.
static float renderShieldStats(const ItemDef& def, EntityManager& em, entt::entity player, float cx,
                               float y, float cw, float val_x)
{
    const float stat_line = FontManager::lineHeight(sBodyFont) + 4.0f;

    UIRenderer::drawText(sBodyFont, def.name, cx, y, ItemStatRenderer::rarityColor(def.rarity));
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
    UIRenderer::drawText(sBodyFont, ItemStatRenderer::formatWeight(def.weight), val_x, y,
                         TEXT_WHITE);
    y += stat_line;

    return y;
}

// renderArmorStats is in ItemStatRenderer.

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
                           float mx, float my, int hoverIdx)
{
    for (int i = 0; i < static_cast<int>(picker.size()); ++i)
    {
        const bool selected = (i == sEquipPickSel);
        const bool hovered = (i == hoverIdx && !selected);
        if (selected)
            UIRenderer::drawRect(cx - 4.0f, y - 2.0f, cw + 8.0f, line_h, SELECTED_BG);
        else if (hovered)
            UIRenderer::drawRect(cx - 4.0f, y - 2.0f, cw + 8.0f, line_h, HOVERED_BG);

        const bool highlighted = selected || hovered;
        const std::string prefix = selected ? "> " : "  ";
        const Color text_color =
            (picker[static_cast<size_t>(i)].inv_index < 0) ? TEXT_DIM : TEXT_WHITE;

        const float icon_sz = line_h - 4.0f;
        const float text_x = cx + icon_sz + 4.0f;
        const auto& pe = picker[static_cast<size_t>(i)];
        const ItemDef* pdef = (pe.inv_index >= 0) ? items.find(pe.config_path) : nullptr;
        ItemStatRenderer::drawItemIcon(pdef, cx, y, icon_sz);
        UIRenderer::drawText(sBodyFont, prefix + pe.label, text_x, y,
                             highlighted ? TEXT_WHITE : text_color);

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
    if (sContentSel < 0 || sContentSel >= EQUIP_SLOT_COUNT)
    {
        sEquipPicking = false;
        return false;
    }
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
    const float y = ey + line_h + 4.0f;

    if (picker.empty())
    {
        UIRenderer::drawText(sBodyFont, "No compatible items.", cx, y, TEXT_DIM);
    }
    else
    {
        sEquipPickSel =
            ((sEquipPickSel % static_cast<int>(picker.size())) + static_cast<int>(picker.size())) %
            static_cast<int>(picker.size());

        const int pickerHover =
            hoveredRow(mx, my, cx, y, cw, line_h, static_cast<int>(picker.size()));
        if (pickerHover >= 0 && mouseClicked(em, SDL_BUTTON_LEFT))
            sEquipPickSel = pickerHover;

        drawPickerRows(em, player, picker, slot_enum, items, cx, y, cw, line_h, mx, my,
                       pickerHover);

        if (confirmKeyPressed(em))
        {
            performEquipAction(em, player, picker[static_cast<size_t>(sEquipPickSel)], slot_enum,
                               items);
            sEquipPicking = false;
        }
    }

    const bool rmbConsumed = mouseClicked(em, SDL_BUTTON_RIGHT);
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
                                float cx, float ey, float cw, float line_h, float mx, float my,
                                int hoverIdx)
{
    float y = ey;
    for (int i = 0; i < EQUIP_SLOT_COUNT; ++i)
    {
        const bool selected = (sContentSel == i && sBottomSel < 0);
        const bool hovered = (i == hoverIdx && !selected);
        const ItemInstance& slot = InventoryOps::slotRef(eq, EQUIP_SLOT_ENUMS[i]);

        if (selected)
            UIRenderer::drawRect(cx - 4.0f, y - 2.0f, cw + 8.0f, line_h, SELECTED_BG);
        else if (hovered)
            UIRenderer::drawRect(cx - 4.0f, y - 2.0f, cw + 8.0f, line_h, HOVERED_BG);

        const float icon_sz = line_h - 4.0f;
        const float text_x = cx + icon_sz + 4.0f;
        std::string text = std::string(EQUIP_SLOT_NAMES[i]) + ": ";
        const bool highlighted = selected || hovered;
        if (slot.empty())
        {
            text += (EQUIP_SLOT_ENUMS[i] == EquipSlot::RightHand) ? "(Unarmed)" : "(empty)";
            UIRenderer::drawText(sBodyFont, text, text_x, y, highlighted ? TEXT_WHITE : TEXT_DIM);
        }
        else
        {
            const ItemDef* def = items.find(slot.config_path);
            const std::string name = (def != nullptr) ? def->name : "???";
            text += std::string(qualityName(slot.quality)) + " " + name;
            ItemStatRenderer::drawItemIcon(def, cx, y, icon_sz);
            UIRenderer::drawText(sBodyFont, text, text_x, y, TEXT_WHITE);
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
    const bool god_mode = em.registry().ctx().get<DebugFlags>().god_mode;

    if (sel_slot == EquipSlot::RightHand)
    {
        float stat_bottom = y;
        if (def != nullptr && def->category == ItemCategory::Weapon)
        {
            stat_bottom = ItemStatRenderer::renderWeaponStatsFromDef(
                sBodyFont, *def, stats, f, has_stats, cx, y, cw, val_x, true, god_mode);
        }
        else
        {
            // Unarmed fallback.
            const Weapon w{"Unarmed", f.fist.weight,     f.fist.str_scaling, f.fist.dex_scaling, 0,
                           0,         f.fist.base_damage};
            stat_bottom = ItemStatRenderer::renderWeaponStats(
                sBodyFont, w, stats, f, nullptr, has_stats, cx, y, cw, val_x, true, god_mode);
        }

        // Weapon XP progress.
        if (em.registry().all_of<WeaponXP>(player))
        {
            const auto& wxp = em.registry().get<WeaponXP>(player);
            const float bar_y = stat_bottom + 4.0f;
            const float bar_h = 10.0f;
            const float fill = wxp.xp_to_next > 0.0f ? wxp.current_xp / wxp.xp_to_next : 0.0f;

            static constexpr Color WPN_BAR{0.45f, 0.55f, 0.85f, 0.9f};
            static constexpr Color WPN_BG{0.12f, 0.15f, 0.30f, 0.6f};

            const std::string lvl_text = "Weapon Lv" + std::to_string(wxp.level);
            UIRenderer::drawText(sBodyFont, lvl_text, cx, bar_y, LABEL_COLOR);
            const float lbl_h = FontManager::lineHeight(sBodyFont);
            UIRenderer::drawRect(cx, bar_y + lbl_h + 2.0f, cw, bar_h, WPN_BG);
            UIRenderer::drawRect(cx, bar_y + lbl_h + 2.0f, cw * std::clamp(fill, 0.0f, 1.0f), bar_h,
                                 WPN_BAR);

            const int xp_cur = static_cast<int>(wxp.current_xp);
            const int xp_max = static_cast<int>(wxp.xp_to_next);
            const std::string xp_text =
                std::to_string(xp_cur) + " / " + std::to_string(xp_max) + " XP";
            const TextSize xpsz = UIRenderer::measureText(sBodyFont, xp_text);
            UIRenderer::drawText(sBodyFont, xp_text, cx + cw - xpsz.width, bar_y, TEXT_DIM);
        }
    }
    else if (sel_slot == EquipSlot::LeftHand && def != nullptr && def->max_guard > 0.0f)
    {
        renderShieldStats(*def, em, player, cx, y, cw, val_x);
    }
    else if (def != nullptr && def->category == ItemCategory::Armor)
    {
        ItemStatRenderer::renderArmorStats(sBodyFont, *def, cx, y, cw, val_x);
    }
    else if (sel_item.empty())
    {
        const std::string empty_label = (sel_slot == EquipSlot::RightHand) ? "(Unarmed)" : "(empty)";
        UIRenderer::drawText(sBodyFont, empty_label, cx, y, TEXT_DIM);
    }
}

// Returns true if right-click was consumed (e.g. closing the picker).
static bool renderEquipmentTab(EntityManager& em, float cx, float cy, float cw, float mx, float my)
{
    const entt::entity player = findPlayer(em);
    if (player == entt::null || !em.registry().all_of<Equipment>(player))
        return false;

    const auto& eq = em.registry().get<Equipment>(player);
    const auto& items = em.registry().ctx().get<ItemRegistry>();
    const float line_h = FontManager::lineHeight(sBodyFont) + 6.0f;
    const float ey = drawTabHeading("Equipment", cx, cy, cw);

    if (sEquipPicking)
        return renderEquipPicker(em, player, eq, items, cx, ey, cw, mx, my);

    const int hover = hoveredRow(mx, my, cx, ey, cw, line_h, EQUIP_SLOT_COUNT);
    if (hover >= 0 && mouseClicked(em, SDL_BUTTON_LEFT))
    {
        sContentSel = hover;
        sBottomSel = -1;
    }

    renderEquipSlotList(em, eq, items, cx, ey, cw, line_h, mx, my, hover);
    const int displaySlot = (hover >= 0) ? hover : sContentSel;
    if (displaySlot >= 0)
    {
        const int savedSel = sContentSel;
        sContentSel = displaySlot;
        renderEquipStatPanel(em, player, eq, items, cx,
                             ey + line_h * static_cast<float>(EQUIP_SLOT_COUNT), cw);
        sContentSel = savedSel;
    }

    if (sBottomSel < 0 && sContentSel >= 0 && confirmKeyPressed(em))
    {
        sEquipPicking = true;
        sEquipPickSel = 0;
    }

    return false;
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
        const entt::entity p = findPlayer(em);
        if (p != entt::null && em.registry().all_of<Inventory>(p))
        {
            maxContentIdx = em.registry().get<Inventory>(p).max_slots - 1;
            hasContent = true;
        }
        break;
    }
    default:
        break;
    }
}

// Handle up-arrow navigation in the pause menu.
static void handleNavUp(UIState::Tab menu_tab, bool hasContent)
{
    if (sBottomSel >= 0)
    {
        sBottomSel = -1;
        if (sContentSel < 0)
            sContentSel = 0;
        return;
    }
    if (!hasContent)
        return;
    if (sContentSel < 0)
    {
        sContentSel = 0;
        return;
    }
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
    if (sContentSel < 0)
    {
        sContentSel = 0;
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
    constexpr int BOTTOM_COUNT = 3; // Resume, Escape, Quit
    if (sBottomSel >= 0)
        sBottomSel = (sBottomSel + direction + BOTTOM_COUNT) % BOTTOM_COUNT;
    else if (sContentSel < 0)
        sContentSel = 0;
    else
        sContentSel += direction;
}

// Handle keyboard navigation in the pause menu.
// Returns: 0 = nothing, 1 = quit app, 2 = escape run.
static int handleMenuKeyInput(EntityManager& em, UIState& ui, int tab, int maxContentIdx,
                              bool hasContent)
{
    if (keyPressed(em, SDL_SCANCODE_Q))
    {
        ui.menu_tab =
            static_cast<UIState::Tab>((tab - 1 + UIState::TAB_COUNT) % UIState::TAB_COUNT);
        sContentSel = -1;
        sBottomSel = -1;
        sEquipPicking = false;
    }
    if (keyPressed(em, SDL_SCANCODE_E))
    {
        ui.menu_tab = static_cast<UIState::Tab>((tab + 1) % UIState::TAB_COUNT);
        sContentSel = -1;
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
            return 2; // escape run
        else if (sBottomSel == 2)
            return 1; // quit app
    }
    return 0;
}

// Draw the tab bar at the top of the pause menu panel.
static void renderTabBar(EntityManager& em, UIState& ui, float panel_x, float panel_y,
                         float /*panel_w*/, float tab_h, float tab_w, float mx, float my)
{
    for (int i = 0; i < UIState::TAB_COUNT; ++i)
    {
        const float tx = panel_x + tab_w * static_cast<float>(i);
        const float ty = panel_y;
        const bool active = (i == static_cast<int>(ui.menu_tab));

        const bool hovered = (mx >= tx && mx < tx + tab_w && my >= ty && my < ty + tab_h);
        if (hovered)
        {
            for (const uint8_t btn : em.mouse_down_events)
            {
                if (btn == SDL_BUTTON_LEFT)
                {
                    ui.menu_tab = static_cast<UIState::Tab>(i);
                    sContentSel = -1;
                    sBottomSel = -1;
                    sEquipPicking = false;
                    screen_input::playClickSfx(em);
                }
            }
        }

        const Color bg = active ? TAB_ACTIVE : (hovered ? TAB_HOVER : TAB_BG);
        UIRenderer::drawRect(tx, ty, tab_w, tab_h, bg);

        const TextSize sz = UIRenderer::measureText(sBodyFont, TAB_NAMES[i]);
        UIRenderer::drawText(sBodyFont, TAB_NAMES[i], tx + (tab_w - sz.width) * 0.5f,
                             ty + (tab_h - sz.height) * 0.5f, active ? TEXT_WHITE : TEXT_DIM);
    }
}

// Draw the bottom bar (hint + Resume/Quit buttons + click handling). Returns true if quit.
// Draw a single bottom-bar button; returns true if hovered.
static bool drawBottomBtn(const std::string& text, float bx, float by, float pad_x, float pad_y,
                          float btn_w, float btn_h, float mx, float my, int selIdx, Color hlColor,
                          Color normalColor)
{
    const bool hover = (mx >= bx && mx < bx + btn_w && my >= by && my < by + btn_h);
    const bool selected = (sBottomSel == selIdx);
    const bool highlighted = hover || selected;
    UIRenderer::drawRect(bx, by, btn_w, btn_h,
                         selected ? BTN_BG_HL : (hover ? HOVERED_BG : BTN_BG));
    UIRenderer::drawText(sBodyFont, text, bx + pad_x, by + pad_y,
                         highlighted ? hlColor : normalColor);
    return hover;
}

// Returns: 0 = nothing, 1 = quit app, 2 = escape run.
static int renderBottomBar(EntityManager& em, UIState& ui, float panel_x, float panel_w,
                           float panel_bottom, float mx, float my, bool equipRmbConsumed)
{
    int action = 0;
    const float line_h = FontManager::lineHeight(sBodyFont);
    const float bottom_pad = 20.0f; // breathing room below buttons
    const float btn_pad_x = 20.0f;
    const float btn_pad_y = 8.0f;
    const float btn_gap = 16.0f;

    const TextSize rsz = UIRenderer::measureText(sBodyFont, "Resume");
    const TextSize esz = UIRenderer::measureText(sBodyFont, "Escape Run");
    const TextSize qsz = UIRenderer::measureText(sBodyFont, "Quit Game");

    const float rBtn_w = rsz.width + btn_pad_x * 2.0f;
    const float eBtn_w = esz.width + btn_pad_x * 2.0f;
    const float qBtn_w = qsz.width + btn_pad_x * 2.0f;
    const float btn_h = std::max({rsz.height, esz.height, qsz.height}) + btn_pad_y * 2.0f;
    const float total_btn_w = rBtn_w + btn_gap + eBtn_w + btn_gap + qBtn_w;
    const float btn_start_x = panel_x + (panel_w - total_btn_w) * 0.5f;

    // Layout bottom-up: buttons -> separator -> hint
    const float btn_y = panel_bottom - bottom_pad - btn_h;
    const float sep_y = btn_y - 12.0f;
    const float sep_inset = 24.0f;
    const float hint_y = sep_y - 6.0f - line_h;

    // Controls hint (centered).
    const std::string hint = sEquipPicking ? "[W/S] Navigate   [F] Equip   [RMB] Back"
                                           : "[Q/E] Tab   [WASD] Navigate   [F] Select";
    const TextSize hintSz = UIRenderer::measureText(sBodyFont, hint);
    UIRenderer::drawText(sBodyFont, hint, panel_x + (panel_w - hintSz.width) * 0.5f, hint_y,
                         TEXT_DIM);

    // Separator.
    UIRenderer::drawRect(panel_x + sep_inset, sep_y, panel_w - sep_inset * 2.0f, 1.0f, HEADING_SEP);

    // Buttons.
    float bx = btn_start_x;
    const bool rHover = drawBottomBtn("Resume", bx, btn_y, btn_pad_x, btn_pad_y, rBtn_w, btn_h, mx,
                                      my, 0, BTN_RESUME_HL, BTN_NORMAL);
    bx += rBtn_w + btn_gap;
    const bool eHover = drawBottomBtn("Escape Run", bx, btn_y, btn_pad_x, btn_pad_y, eBtn_w, btn_h,
                                      mx, my, 1, BTN_ESCAPE_HL, BTN_ESCAPE);
    bx += eBtn_w + btn_gap;
    const bool qHover = drawBottomBtn("Quit Game", bx, btn_y, btn_pad_x, btn_pad_y, qBtn_w, btn_h,
                                      mx, my, 2, BTN_QUIT_HL, BTN_QUIT);
    // Mouse click on buttons.
    const int clickedBtn = rHover ? 0 : (eHover ? 1 : (qHover ? 2 : -1));
    for (const uint8_t btn : em.mouse_down_events)
    {
        if (btn == SDL_BUTTON_LEFT && clickedBtn >= 0)
        {
            sBottomSel = clickedBtn;
            screen_input::playClickSfx(em);
            if (clickedBtn == 0)
                ui.active_screen = UIState::Screen::None;
            else if (clickedBtn == 1)
                action = 2; // escape run
            else if (clickedBtn == 2)
                action = 1; // quit app
        }
        if (btn == SDL_BUTTON_RIGHT && !equipRmbConsumed)
        {
            screen_input::playClickSfx(em);
            ui.active_screen = UIState::Screen::None;
        }
    }

    return action;
}

// ---------------------------------------------------------------------------
// Main render
// ---------------------------------------------------------------------------

int PauseMenu::render(EntityManager& em, int window_w, int window_h)
{
    ZoneScopedN("PauseMenu");

    const float ww = static_cast<float>(window_w);
    const float wh = static_cast<float>(window_h);
    auto& ui = em.registry().ctx().get<UIState>();
    const int tab = static_cast<int>(ui.menu_tab);
    int action = 0; // 0 = nothing, 1 = quit app, 2 = escape run

    int maxContentIdx = 0;
    bool hasContent = false;
    computeMaxContentIndex(em, ui.menu_tab, maxContentIdx, hasContent);

    // Input (skipped when equipment picker is active -- picker handles its own keys).
    if (!sEquipPicking)
        action = handleMenuKeyInput(em, ui, tab, maxContentIdx, hasContent);

    // --- Mouse state ---
    int mouseX = 0;
    int mouseY = 0;
    SDL_GetMouseState(&mouseX, &mouseY);
    const float mx = static_cast<float>(mouseX);
    const float my = static_cast<float>(mouseY);

    // --- Draw: overlay + panel ---
    UIRenderer::drawRect(0.0f, 0.0f, ww, wh, OVERLAY);

    const float panel_w = 520.0f;
    const float panel_h = 700.0f;
    const float panel_x = (ww - panel_w) * 0.5f;
    const float panel_y = (wh - panel_h) * 0.5f;
    UIRenderer::drawRect(panel_x, panel_y, panel_w, panel_h, PAUSE_BG);

    // --- Draw: tab bar ---
    const float tab_h = FontManager::lineHeight(sBodyFont) + 12.0f;
    const float tab_w = panel_w / static_cast<float>(UIState::TAB_COUNT);
    renderTabBar(em, ui, panel_x, panel_y, panel_w, tab_h, tab_w, mx, my);

    // --- Draw: content area ---
    // Footer height computed bottom-up: bottom_pad + btn_h + gap + sep + gap + hint + gap.
    // Approximate footer reservation (generous to avoid overlap).
    const float footer_reserve = 100.0f;
    const float content_x = panel_x + 24.0f;
    const float content_y = panel_y + tab_h + 20.0f;
    const float content_w = panel_w - 48.0f;
    const float content_h = panel_h - tab_h - 20.0f - footer_reserve;

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
    default:
        break;
    }

    // --- Draw: bottom bar (laid out bottom-up from panel edge) ---
    const float panel_bottom = panel_y + panel_h;
    if (action == 0)
        action = renderBottomBar(em, ui, panel_x, panel_w, panel_bottom, mx, my, equipRmbConsumed);

    return action;
}
