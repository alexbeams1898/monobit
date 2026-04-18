#include "screens/InventoryScreen.h"

#include "UIRenderer.h"
#include "ecs/Components.h"
#include "ecs/GameComponents.h"
#include "ecs/GameConfig.h"
#include "ops/InventoryOps.h"
#include "renderers/ItemStatRenderer.h"
#include "screens/ScreenColors.h"

#include <SDL.h>
#include <string>
#include <tracy/Tracy.hpp>

using namespace screen_colors;

static FontHandle sBodyFont = INVALID_FONT;
static FontHandle sTitleFont = INVALID_FONT;
static int sSelectedSlot = 0;

static constexpr Color TITLE_COLOR{1.0f, 0.85f, 0.3f, 1.0f};
static constexpr Color SLOT_BG{0.15f, 0.15f, 0.15f, 0.8f};
static constexpr Color SLOT_SELECTED{0.3f, 0.3f, 0.5f, 0.9f};
static constexpr Color SLOT_EMPTY{0.4f, 0.4f, 0.4f, 0.5f};
static constexpr Color EQUIP_LABEL{0.5f, 0.8f, 1.0f, 1.0f};
static constexpr Color INV_BG{0.08f, 0.08f, 0.12f, 0.9f};

// Touch-friendly slot size. Apple HIG's 44pt is a bare minimum that assumes
// dense Retina mapping; on a 1920x1080 logical canvas that floor maps to a
// pretty small finger target. 72px (50% above the floor) feels comfortable
// for couch / phone play while still leaving the inventory panel visually
// balanced against the pause menu. Mouse + keyboard play only benefits.
static constexpr float SLOT_SIZE = 72.0f;
static constexpr float SLOT_GAP = 10.0f;
static constexpr int GRID_COLS = 10;

static constexpr Color EQUIP_R_BORDER{0.3f, 0.6f, 1.0f, 0.8f};
static constexpr Color EQUIP_L_BORDER{0.3f, 1.0f, 0.5f, 0.8f};

static void renderInventoryGrid(const Inventory& inv, const Equipment& equip,
                                const ItemRegistry& items, float panel_x, float ey, int total_slots)
{
    for (int i = 0; i < total_slots; ++i)
    {
        const int col = i % GRID_COLS;
        const int row = i / GRID_COLS;
        const float sx = panel_x + 24.0f + static_cast<float>(col) * (SLOT_SIZE + SLOT_GAP);
        const float sy = ey + static_cast<float>(row) * (SLOT_SIZE + SLOT_GAP);

        const bool selected = (i == sSelectedSlot);
        const Color bg = selected ? SLOT_SELECTED : SLOT_BG;
        UIRenderer::drawRect(sx, sy, SLOT_SIZE, SLOT_SIZE, bg);

        if (i < static_cast<int>(inv.items.size()) && !inv.items[static_cast<size_t>(i)].empty())
        {
            const auto& item = inv.items[static_cast<size_t>(i)];
            const ItemDef* def = items.find(item.config_path);
            const float icon_pad = 6.0f;
            ItemStatRenderer::drawItemIcon(def, sx + icon_pad, sy + icon_pad,
                                           SLOT_SIZE - icon_pad * 2.0f);

            // Quantity badge: bottom-right corner of the slot.
            if (item.quantity > 1)
            {
                const std::string qty = std::to_string(item.quantity);
                const TextSize qsz = UIRenderer::measureText(sBodyFont, qty);
                UIRenderer::drawText(sBodyFont, qty, sx + SLOT_SIZE - qsz.width - 4.0f,
                                     sy + SLOT_SIZE - qsz.height - 4.0f, TEXT_WHITE);
            }

            // Equipped hand badge: colored border + label in top-left.
            const bool isRH = (equip.right_hand == i);
            const bool isLH = (equip.left_hand == i);
            if (isRH || isLH)
            {
                const Color& bc = isRH ? EQUIP_R_BORDER : EQUIP_L_BORDER;
                const float b = 2.0f;
                UIRenderer::drawRect(sx, sy, SLOT_SIZE, b, bc);
                UIRenderer::drawRect(sx, sy + SLOT_SIZE - b, SLOT_SIZE, b, bc);
                UIRenderer::drawRect(sx, sy, b, SLOT_SIZE, bc);
                UIRenderer::drawRect(sx + SLOT_SIZE - b, sy, b, SLOT_SIZE, bc);
                const char* badge = isRH ? "R" : "L";
                UIRenderer::drawText(sBodyFont, badge, sx + 3.0f, sy + 1.0f, bc);
            }
        }
        else
        {
            UIRenderer::drawRect(sx + 2.0f, sy + 2.0f, SLOT_SIZE - 4.0f, SLOT_SIZE - 4.0f,
                                 SLOT_EMPTY);
        }
    }
}

static void renderItemDetail(const Inventory& inv, const ItemRegistry& items, float panel_x,
                             float detail_y)
{
    if (sSelectedSlot < static_cast<int>(inv.items.size()) &&
        !inv.items[static_cast<size_t>(sSelectedSlot)].empty())
    {
        const auto& item = inv.items[static_cast<size_t>(sSelectedSlot)];
        const ItemDef* def = items.find(item.config_path);

        if (def != nullptr)
        {
            UIRenderer::drawText(sBodyFont, def->name, panel_x + 16.0f, detail_y,
                                 ItemStatRenderer::rarityColor(def->rarity));
            UIRenderer::drawText(sBodyFont, def->description, panel_x + 16.0f,
                                 detail_y + FontManager::lineHeight(sBodyFont) + 2.0f, TEXT_DIM);
        }
    }
}

void InventoryScreen::init(FontHandle body_font, FontHandle title_font)
{
    sBodyFont = body_font;
    sTitleFont = title_font;
    sSelectedSlot = 0;
}

void InventoryScreen::reset()
{
    sSelectedSlot = 0;
}

void InventoryScreen::render(EntityManager& em, int window_w, int window_h)
{
    ZoneScopedN("InventoryScreen");

    const float ww = static_cast<float>(window_w);
    const float wh = static_cast<float>(window_h);

    // Process navigation from event buffer.
    for (const int key : em.key_down_events)
    {
        if (key == SDL_SCANCODE_UP || key == SDL_SCANCODE_W)
            sSelectedSlot -= GRID_COLS;
        else if (key == SDL_SCANCODE_DOWN || key == SDL_SCANCODE_S)
            sSelectedSlot += GRID_COLS;
        else if (key == SDL_SCANCODE_LEFT || key == SDL_SCANCODE_A)
            sSelectedSlot--;
        else if (key == SDL_SCANCODE_RIGHT || key == SDL_SCANCODE_D)
            sSelectedSlot++;
    }

    // Full-screen overlay.
    UIRenderer::drawRect(0.0f, 0.0f, ww, wh, OVERLAY);

    // Find player.
    entt::entity player = entt::null;
    for (auto e : em.registry().view<PlayerActions>())
    {
        player = e;
        break;
    }
    if (player == entt::null)
        return;

    // Inventory panel. Width fits a 10-col grid of 72px touch-target slots
    // (10 * (72 + 10) - 10 + 24 left pad + 24 right pad = 858). Height fits
    // 6 equipment label rows + 3 inventory grid rows (3 * 82 - 10 = 236) +
    // detail/footer area.
    const float panel_w = 880.0f;
    const float panel_h = 640.0f;
    const float panel_x = (ww - panel_w) * 0.5f;
    const float panel_y = (wh - panel_h) * 0.5f;
    UIRenderer::drawRect(panel_x, panel_y, panel_w, panel_h, INV_BG);

    // Title.
    UIRenderer::drawText(sTitleFont, "Inventory", panel_x + 16.0f, panel_y + 12.0f, TITLE_COLOR);

    // Equipment section.
    float ey = panel_y + 48.0f;
    UIRenderer::drawText(sBodyFont, "Equipment:", panel_x + 16.0f, ey, EQUIP_LABEL);
    ey += FontManager::lineHeight(sBodyFont) + 4.0f;

    if (em.registry().all_of<Equipment, Inventory>(player))
    {
        const auto& eq = em.registry().get<Equipment>(player);
        const auto& inv = em.registry().get<Inventory>(player);
        const auto& items = em.registry().ctx().get<ItemRegistry>();

        auto drawSlotLabel = [&](const char* label, EquipSlot slot)
        {
            const auto* item = InventoryOps::equippedItem(inv, eq, slot);
            std::string text = std::string(label) + ": ";
            if (item == nullptr)
            {
                text += "(Unarmed)";
                UIRenderer::drawText(sBodyFont, text, panel_x + 24.0f, ey, TEXT_DIM);
            }
            else
            {
                const ItemDef* def = items.find(item->config_path);
                const std::string name = (def != nullptr) ? def->name : "???";
                text += qualityName(item->quality) + std::string(" ") + name;
                UIRenderer::drawText(sBodyFont, text, panel_x + 24.0f, ey, TEXT_WHITE);
            }
            ey += FontManager::lineHeight(sBodyFont) + 2.0f;
        };

        drawSlotLabel("Right Hand", EquipSlot::RightHand);
        drawSlotLabel("Left Hand", EquipSlot::LeftHand);
        drawSlotLabel("Head", EquipSlot::Head);
        drawSlotLabel("Chest", EquipSlot::Chest);
        drawSlotLabel("Legs", EquipSlot::Legs);
        drawSlotLabel("Feet", EquipSlot::Feet);
    }

    // Inventory grid.
    ey += 8.0f;
    UIRenderer::drawText(sBodyFont, "Items:", panel_x + 16.0f, ey, EQUIP_LABEL);
    ey += FontManager::lineHeight(sBodyFont) + 4.0f;

    if (em.registry().all_of<Inventory>(player))
    {
        const auto& inv = em.registry().get<Inventory>(player);
        const auto& items = em.registry().ctx().get<ItemRegistry>();

        const int total_slots = inv.max_slots;
        sSelectedSlot = ((sSelectedSlot % total_slots) + total_slots) % total_slots; // wrap around

        const auto& gridEquip = em.registry().all_of<Equipment>(player)
                                    ? em.registry().get<Equipment>(player)
                                    : Equipment{};
        renderInventoryGrid(inv, gridEquip, items, panel_x, ey, total_slots);

        // Selected item detail.
        const float detail_y = panel_y + panel_h - 60.0f;
        renderItemDetail(inv, items, panel_x, detail_y);
    }

    // Controls hint.
    UIRenderer::drawText(sBodyFont, "[I] Close   [W/S] Navigate", panel_x + 16.0f,
                         panel_y + panel_h - 24.0f, TEXT_DIM);
}
