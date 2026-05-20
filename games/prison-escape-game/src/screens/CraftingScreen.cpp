#include "screens/CraftingScreen.h"

#include "UIRenderer.h"
#include "ecs/Components.h"
#include "ecs/GameComponents.h"
#include "ecs/GameConfig.h"
#include "ecs/ItemConfig.h"
#include "ops/CraftingOps.h"
#include "renderers/ItemStatRenderer.h"
#include "screens/ScreenColors.h"
#include "screens/ScreenInput.h"
#include "systems/AudioSystem.h"
#include "systems/NotificationSystem.h"

#include <tracy/Tracy.hpp>

#include <SDL.h>

#include <algorithm>
#include <string>
#include <vector>

#include <glad/glad.h>

using screen_input::keyPressed;
using screen_input::mouseClicked;
using namespace screen_colors;

static FontHandle sBodyFont = INVALID_FONT;
static FontHandle sTitleFont = INVALID_FONT;
static int sSelectedRecipe = 0;
static int sHoveredRecipe = -1;
static float sScrollOffset = 0.0f;

static constexpr Color TITLE_COLOR{1.0f, 0.85f, 0.3f, 1.0f};
static constexpr Color HEADER_COLOR{0.6f, 0.85f, 0.7f, 0.8f};
static constexpr Color HAVE_COLOR{0.3f, 0.9f, 0.3f, 1.0f};
static constexpr Color NEED_COLOR{0.9f, 0.3f, 0.3f, 1.0f};
static constexpr Color SELECTED_BG{0.2f, 0.3f, 0.25f, 0.6f};
static constexpr Color SEPARATOR{0.3f, 0.4f, 0.35f, 0.5f};
static constexpr Color HINT_COLOR{0.5f, 0.48f, 0.46f, 0.8f};
static constexpr Color DESC_COLOR{0.7f, 0.68f, 0.55f, 0.9f};
static constexpr Color CRAFT_BTN_BG{0.15f, 0.35f, 0.2f, 0.8f};
static constexpr Color CRAFT_BTN_HL{0.2f, 0.5f, 0.3f, 0.9f};
static constexpr Color CRAFT_BTN_OFF{0.15f, 0.15f, 0.18f, 0.5f};
static constexpr Color CRAFT_BTN_TEXT{0.9f, 0.88f, 0.8f, 1.0f};
static constexpr Color CRAFT_BTN_TEXT_OFF{0.4f, 0.38f, 0.36f, 0.6f};

static void playSfx(const SoundConfig& snd)
{
    if (!snd.get("ui_click").path.empty())
        AudioSystem::playSfx(snd.get("ui_click").path, snd.get("ui_click").volume);
}

static int countItem(const Inventory& inv, const std::string& config_path)
{
    int total = 0;
    for (const auto& item : inv.items)
        if (item.config_path == config_path)
            total += item.quantity;
    return total;
}

// Preview the output quality tier without actually consuming materials.
// Mirrors CraftingOps::craft logic: uses worst-quality stacks first.
static QualityTier previewOutputQuality(const Inventory& inv, const RecipeDef& recipe)
{
    int totalQuality = 0;
    int totalItems = 0;
    for (const auto& ing : recipe.inputs)
    {
        // Gather matching stacks sorted by quality ascending (worst first).
        std::vector<std::pair<QualityTier, int>> stacks;
        for (const auto& slot : inv.items)
            if (slot.config_path == ing.config_path)
                stacks.emplace_back(slot.quality, slot.quantity);
        std::sort(stacks.begin(), stacks.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });

        int remaining = ing.quantity;
        for (const auto& [q, qty] : stacks)
        {
            if (remaining <= 0)
                break;
            const int take = std::min(remaining, qty);
            totalQuality += static_cast<int>(q) * take;
            remaining -= take;
        }
        totalItems += ing.quantity;
    }

    const int avg = (totalItems > 0) ? ((totalQuality + totalItems / 2) / totalItems) : 1;
    return static_cast<QualityTier>(std::min(avg, static_cast<int>(QualityTier::Masterwork)));
}

static int categoryOrder(ItemCategory c)
{
    switch (c)
    {
    case ItemCategory::Weapon:
        return 0;
    case ItemCategory::Armor:
        return 1;
    case ItemCategory::Accessory:
        return 2;
    case ItemCategory::Consumable:
        return 3;
    case ItemCategory::Material:
        return 4;
    case ItemCategory::KeyItem:
        return 5;
    case ItemCategory::Money:
        return 6;
    }
    return 99;
}

static const char* categoryName(ItemCategory c)
{
    switch (c)
    {
    case ItemCategory::Weapon:
        return "Weapons";
    case ItemCategory::Armor:
        return "Armor";
    case ItemCategory::Accessory:
        return "Accessories";
    case ItemCategory::Consumable:
        return "Consumables";
    case ItemCategory::Material:
        return "Materials";
    case ItemCategory::KeyItem:
        return "Key Items";
    case ItemCategory::Money:
        return "Currency";
    }
    return "Other";
}

// A row in the recipe list: either a category header or a selectable recipe.
struct DisplayRow
{
    bool is_header = false;
    int recipe_index = -1;
    std::string text;
};

// Shared layout parameters for render sub-functions.
struct CraftLayout
{
    float px, cx, cw, panel_w;
    float line_h, sep_gap;
    float scroll_top, scroll_bottom;
    float mx, my;
};

// Measure all recipes to find the maximum content width and detail height.
static void measureAllRecipes(const RecipeRegistry& recipes, const ItemRegistry& items,
                              float stat_line, float line_h, float& content_w, float& detail_h,
                              bool& anyHasRequirements)
{
    const int recipe_count = static_cast<int>(recipes.recipes.size());
    for (int ri = 0; ri < recipe_count; ++ri)
    {
        const auto& r = recipes.recipes[static_cast<size_t>(ri)];
        const ItemDef* odef = items.find(r.output_item);
        const std::string oname = (odef != nullptr) ? odef->name : r.output_item;

        const float name_icon_w = FontManager::lineHeight(sTitleFont) + 4.0f;
        const TextSize nsz = UIRenderer::measureText(sTitleFont, oname);
        content_w = std::max(content_w, name_icon_w + nsz.width);

        if (odef != nullptr && !odef->description.empty())
        {
            const TextSize dsz = UIRenderer::measureText(sBodyFont, odef->description);
            content_w = std::max(content_w, dsz.width);
        }

        std::string reqLine = "Requires: ";
        for (size_t ii = 0; ii < r.inputs.size(); ++ii)
        {
            const ItemDef* idef = items.find(r.inputs[ii].config_path);
            const std::string iname = (idef != nullptr) ? idef->name : r.inputs[ii].config_path;
            reqLine += iname + " 99/99";
            if (ii + 1 < r.inputs.size())
                reqLine += ", ";
        }
        const TextSize rlsz = UIRenderer::measureText(sBodyFont, reqLine);
        content_w = std::max(content_w, rlsz.width);

        const std::string resultText = "Result: " + oname + " (Masterwork)";
        const TextSize rtsz = UIRenderer::measureText(sBodyFont, resultText);
        content_w = std::max(content_w, rtsz.width);

        if (odef != nullptr && (odef->str_requirement > 0 || odef->dex_requirement > 0))
            anyHasRequirements = true;
    }

    detail_h += FontManager::lineHeight(sTitleFont) + 4.0f;
    detail_h += 1.0f + 6.0f;
    detail_h += line_h;
    detail_h += line_h;
    detail_h += line_h;
    detail_h += 4.0f * stat_line;
    if (anyHasRequirements)
        detail_h += stat_line;
}

// Render the detail section for the hovered/selected recipe.
static void renderRecipeDetail(const RecipeDef& recipe, const ItemDef* output_def,
                               const std::string& outputName, const Inventory* inv,
                               entt::entity player, EntityManager& em, const CraftLayout& lay,
                               float& y)
{
    y += lay.sep_gap;
    UIRenderer::drawRect(lay.cx, y, lay.cw, 1.0f, SEPARATOR);
    y += 1.0f + lay.sep_gap;

    const float detail_icon_sz = FontManager::lineHeight(sTitleFont);
    ItemStatRenderer::drawItemIcon(output_def, lay.cx, y, detail_icon_sz);
    UIRenderer::drawText(sTitleFont, outputName, lay.cx + detail_icon_sz + 4.0f, y, TEXT_WHITE);
    y += FontManager::lineHeight(sTitleFont) + 4.0f;

    UIRenderer::drawRect(lay.cx, y, lay.cw, 1.0f, SEPARATOR);
    y += 6.0f;

    if (output_def != nullptr && !output_def->description.empty())
    {
        UIRenderer::drawText(sBodyFont, output_def->description, lay.cx, y, DESC_COLOR);
        y += lay.line_h;
    }

    // Inline requires line.
    {
        const std::string reqLabel = "Requires: ";
        UIRenderer::drawText(sBodyFont, reqLabel, lay.cx, y, TEXT_DIM);
        float rx = lay.cx + UIRenderer::measureText(sBodyFont, reqLabel).width;

        for (size_t ii = 0; ii < recipe.inputs.size(); ++ii)
        {
            const auto& ing = recipe.inputs[ii];
            const ItemDef* idef = em.registry().ctx().get<ItemRegistry>().find(ing.config_path);
            const std::string iname = (idef != nullptr) ? idef->name : ing.config_path;
            const int have = (inv != nullptr) ? countItem(*inv, ing.config_path) : 0;
            const Color c = (have >= ing.quantity) ? HAVE_COLOR : NEED_COLOR;
            const std::string part =
                iname + " " + std::to_string(have) + "/" + std::to_string(ing.quantity);
            UIRenderer::drawText(sBodyFont, part, rx, y, c);
            rx += UIRenderer::measureText(sBodyFont, part).width;
            if (ii + 1 < recipe.inputs.size())
            {
                UIRenderer::drawText(sBodyFont, ", ", rx, y, TEXT_DIM);
                rx += UIRenderer::measureText(sBodyFont, ", ").width;
            }
        }
        y += lay.line_h;
    }

    // Result line with quality preview.
    {
        const bool craftable =
            (inv != nullptr) &&
            CraftingOps::canCraft(*inv, recipe, em.registry().ctx().get<ItemRegistry>());
        const Color resultColor = craftable ? HAVE_COLOR : NEED_COLOR;

        std::string resultText = "Result: " + outputName;
        if (inv != nullptr && craftable)
        {
            const QualityTier q = previewOutputQuality(*inv, recipe);
            resultText += " (";
            resultText += qualityName(q);
            resultText += ")";
        }
        else if (recipe.output_quantity > 1)
        {
            resultText += " x" + std::to_string(recipe.output_quantity);
        }
        UIRenderer::drawText(sBodyFont, resultText, lay.cx, y, resultColor);
        y += lay.line_h;
    }

    // Item stat panel.
    if (output_def != nullptr)
    {
        const bool has_stats = (player != entt::null && em.registry().all_of<Stats>(player));
        const Stats& stats = has_stats ? em.registry().get<Stats>(player) : Stats{1, 1, 1, 1};
        const auto& f = em.registry().ctx().get<FormulaConfig>();
        const bool god_mode = em.registry().ctx().get<DebugFlags>().god_mode;
        const float val_x = lay.cx + 100.0f;
        static_cast<void>(ItemStatRenderer::renderItemStats(sBodyFont, *output_def, stats, f,
                                                            has_stats, lay.cx, y, lay.cw, val_x,
                                                            false, god_mode));
    }
}

// Render the craft button, separator, and hint text.
static void renderCraftFooter(EntityManager& em, entt::entity player, bool canCraft,
                              const CraftLayout& lay, float footer_y, float btn_h,
                              const std::string& hintText, const TextSize& hintsz)
{
    const auto& recipes = em.registry().ctx().get<RecipeRegistry>();
    const auto& items = em.registry().ctx().get<ItemRegistry>();
    const auto& snd = em.registry().ctx().get<SoundConfig>();

    float fy = footer_y;

    const std::string craftLabel = "Craft";
    const TextSize csz = UIRenderer::measureText(sTitleFont, craftLabel);
    const float btn_w = csz.width + 40.0f;
    const float btn_x = lay.cx + (lay.cw - btn_w) * 0.5f;
    const bool btnHovered = canCraft && lay.mx >= btn_x && lay.mx < btn_x + btn_w && lay.my >= fy &&
                            lay.my < fy + btn_h;

    Color btnBg = CRAFT_BTN_OFF;
    Color btnText = CRAFT_BTN_TEXT_OFF;
    if (canCraft)
    {
        btnBg = btnHovered ? CRAFT_BTN_HL : CRAFT_BTN_BG;
        btnText = CRAFT_BTN_TEXT;
    }
    UIRenderer::drawRect(btn_x, fy, btn_w, btn_h, btnBg);
    UIRenderer::drawText(sTitleFont, craftLabel, btn_x + (btn_w - csz.width) * 0.5f,
                         fy + (btn_h - csz.height) * 0.5f, btnText);

    if (btnHovered && mouseClicked(em, SDL_BUTTON_LEFT) && sSelectedRecipe >= 0 &&
        player != entt::null)
    {
        auto& playerInv = em.registry().get<Inventory>(player);
        const auto& recipe = recipes.recipes[static_cast<size_t>(sSelectedRecipe)];
        const bool godMode = em.registry().ctx().get<DebugFlags>().god_mode;
        if (CraftingOps::craft(playerInv, recipe, items, godMode))
        {
            const ItemDef* odef = items.find(recipe.output_item);
            const std::string name = (odef != nullptr) ? odef->name : recipe.output_item;
            NotificationSystem::push("Crafted " + name, {0.3f, 0.9f, 0.3f, 1.0f});
            playSfx(snd);
        }
    }
    fy += btn_h + lay.sep_gap;

    UIRenderer::drawRect(lay.cx, fy, lay.cw, 1.0f, SEPARATOR);
    fy += 1.0f + lay.sep_gap;
    UIRenderer::drawText(sBodyFont, hintText, lay.px + (lay.panel_w - hintsz.width) * 0.5f, fy,
                         HINT_COLOR);
}

// Build grouped display rows sorted by output item category.
static std::vector<DisplayRow> buildDisplayRows(const RecipeRegistry& recipes,
                                                const ItemRegistry& items)
{
    const int count = static_cast<int>(recipes.recipes.size());

    // Pair each recipe with its output category for sorting.
    struct Entry
    {
        int index;
        ItemCategory category;
    };
    std::vector<Entry> entries;
    entries.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i)
    {
        ItemCategory cat = ItemCategory::Material;
        const ItemDef* def = items.find(recipes.recipes[static_cast<size_t>(i)].output_item);
        if (def != nullptr)
            cat = def->category;
        entries.push_back({i, cat});
    }

    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b)
              { return categoryOrder(a.category) < categoryOrder(b.category); });

    std::vector<DisplayRow> rows;
    ItemCategory prevCat = static_cast<ItemCategory>(255);
    for (const auto& e : entries)
    {
        if (e.category != prevCat)
        {
            std::string header = "-- ";
            header += categoryName(e.category);
            header += " --";
            rows.push_back({true, -1, header});
            prevCat = e.category;
        }
        rows.push_back({false, e.index, recipes.recipes[static_cast<size_t>(e.index)].name});
    }
    return rows;
}

static entt::entity findPlayerEntity(entt::registry& reg)
{
    for (auto e : reg.view<PlayerActions>())
        return e;
    return entt::null;
}

static void validateSelection(const std::vector<int>& navOrder)
{
    if (sSelectedRecipe >= 0 && !navOrder.empty())
    {
        bool found = false;
        for (const int idx : navOrder)
            if (idx == sSelectedRecipe)
            {
                found = true;
                break;
            }
        if (!found)
            sSelectedRecipe = -1;
    }
}

static void handleKeyboardNav(EntityManager& em, const std::vector<int>& navOrder)
{
    if (navOrder.empty())
        return;

    int navPos = -1;
    for (int i = 0; i < static_cast<int>(navOrder.size()); ++i)
        if (navOrder[static_cast<size_t>(i)] == sSelectedRecipe)
        {
            navPos = i;
            break;
        }

    if (keyPressed(em, SDL_SCANCODE_UP) || keyPressed(em, SDL_SCANCODE_W))
    {
        if (navPos < 0)
            navPos = static_cast<int>(navOrder.size()) - 1;
        else
            navPos = (navPos - 1 + static_cast<int>(navOrder.size())) %
                     static_cast<int>(navOrder.size());
        sSelectedRecipe = navOrder[static_cast<size_t>(navPos)];
    }
    if (keyPressed(em, SDL_SCANCODE_DOWN) || keyPressed(em, SDL_SCANCODE_S))
    {
        if (navPos < 0)
            navPos = 0;
        else
            navPos = (navPos + 1) % static_cast<int>(navOrder.size());
        sSelectedRecipe = navOrder[static_cast<size_t>(navPos)];
    }
}

static bool handleCraftAction(EntityManager& em, const RecipeRegistry& recipes,
                              const ItemRegistry& items, const SoundConfig& snd,
                              const Inventory*& inv, entt::entity player, bool canCraft)
{
    if (sSelectedRecipe < 0 ||
        !(keyPressed(em, SDL_SCANCODE_RETURN) || keyPressed(em, SDL_SCANCODE_KP_ENTER)) ||
        !canCraft || player == entt::null)
        return canCraft;

    auto& playerInv = em.registry().get<Inventory>(player);
    const auto& recipe = recipes.recipes[static_cast<size_t>(sSelectedRecipe)];
    const bool godCraft = em.registry().ctx().get<DebugFlags>().god_mode;
    if (CraftingOps::craft(playerInv, recipe, items, godCraft))
    {
        const ItemDef* output_def = items.find(recipe.output_item);
        const std::string name = (output_def != nullptr) ? output_def->name : recipe.output_item;
        NotificationSystem::push("Crafted " + name, {0.3f, 0.9f, 0.3f, 1.0f});
        playSfx(snd);
        // Re-fetch inventory pointer after mutation.
        inv = &em.registry().get<Inventory>(player);
        canCraft = CraftingOps::canCraft(*inv, recipe, items);
    }
    return canCraft;
}

static void measureRecipeRows(const std::vector<DisplayRow>& displayRows, float line_h,
                              float header_gap, float& content_w, float& list_h)
{
    for (size_t i = 0; i < displayRows.size(); ++i)
    {
        const auto& row = displayRows[i];
        if (row.is_header)
        {
            if (i > 0)
                list_h += header_gap;
            const TextSize hsz = UIRenderer::measureText(sBodyFont, row.text);
            content_w = std::max(content_w, hsz.width);
            list_h += line_h;
        }
        else
        {
            const float row_icon_w = line_h - 4.0f + 4.0f; // icon + gap
            const TextSize rsz = UIRenderer::measureText(sBodyFont, "> " + row.text);
            content_w = std::max(content_w, row_icon_w + rsz.width);
            list_h += line_h;
        }
    }
}

static void handleAutoScroll(const std::vector<DisplayRow>& displayRows, float visible_scroll_h,
                             float max_scroll, float line_h, float header_gap)
{
    if (visible_scroll_h <= 0.0f)
        return;

    float itemY = 0.0f;
    for (size_t i = 0; i < displayRows.size(); ++i)
    {
        const auto& row = displayRows[i];
        if (!row.is_header && row.recipe_index == sSelectedRecipe)
            break;
        if (row.is_header && i > 0)
            itemY += header_gap;
        itemY += line_h;
    }
    if (itemY < sScrollOffset)
        sScrollOffset = itemY;
    else if (itemY + line_h > sScrollOffset + visible_scroll_h)
        sScrollOffset = itemY + line_h - visible_scroll_h;
    sScrollOffset = std::clamp(sScrollOffset, 0.0f, max_scroll);
}

static void drawRecipeList(const std::vector<DisplayRow>& displayRows, EntityManager& em,
                           const CraftLayout& lay, float header_gap, float& y)
{
    const auto& recipes = em.registry().ctx().get<RecipeRegistry>();
    const auto& items = em.registry().ctx().get<ItemRegistry>();

    for (size_t i = 0; i < displayRows.size(); ++i)
    {
        const auto& row = displayRows[i];

        if (row.is_header)
        {
            if (i > 0)
                y += header_gap;
            UIRenderer::drawText(sBodyFont, row.text, lay.cx, y, HEADER_COLOR);
            y += lay.line_h;
            continue;
        }

        const bool inScroll = (lay.my >= lay.scroll_top && lay.my < lay.scroll_bottom);
        const bool hovered =
            inScroll && (lay.mx >= lay.cx - 4.0f && lay.mx < lay.cx + lay.cw + 4.0f &&
                         lay.my >= y - 2.0f && lay.my < y - 2.0f + lay.line_h);
        if (hovered)
        {
            sHoveredRecipe = row.recipe_index;
            if (mouseClicked(em, SDL_BUTTON_LEFT))
                sSelectedRecipe = row.recipe_index;
        }

        const bool selected = (row.recipe_index == sSelectedRecipe);
        if (selected)
            UIRenderer::drawRect(lay.cx - 4.0f, y - 2.0f, lay.cw + 8.0f, lay.line_h, SELECTED_BG);
        else if (hovered)
            UIRenderer::drawRect(lay.cx - 4.0f, y - 2.0f, lay.cw + 8.0f, lay.line_h, HOVERED_BG);

        const float icon_sz = lay.line_h - 4.0f;
        const float text_x = lay.cx + icon_sz + 4.0f;
        const ItemDef* row_def =
            (row.recipe_index >= 0)
                ? items.find(recipes.recipes[static_cast<size_t>(row.recipe_index)].output_item)
                : nullptr;
        ItemStatRenderer::drawItemIcon(row_def, lay.cx, y, icon_sz);
        const std::string prefix = selected ? "> " : "  ";
        UIRenderer::drawText(sBodyFont, prefix + row.text, text_x, y,
                             selected ? TEXT_WHITE : TEXT_DIM);

        y += lay.line_h;
    }
}

void CraftingScreen::init(FontHandle body_font, FontHandle title_font)
{
    sBodyFont = body_font;
    sTitleFont = title_font;
    sSelectedRecipe = 0;
}

void CraftingScreen::reset()
{
    sSelectedRecipe = -1; // Will snap to first display-order item on next render.
    sHoveredRecipe = -1;
    sScrollOffset = 0.0f;
}

void CraftingScreen::render(EntityManager& em, int window_w, int window_h)
{
    ZoneScopedN("CraftingScreen");

    const float ww = static_cast<float>(window_w);
    const float wh = static_cast<float>(window_h);

    const auto& recipes = em.registry().ctx().get<RecipeRegistry>();
    const auto& items = em.registry().ctx().get<ItemRegistry>();
    const auto& snd = em.registry().ctx().get<SoundConfig>();
    auto& ui = em.registry().ctx().get<UIState>();
    const int recipe_count = static_cast<int>(recipes.recipes.size());

    UIRenderer::drawRect(0.0f, 0.0f, ww, wh, OVERLAY);

    if (keyPressed(em, SDL_SCANCODE_ESCAPE) || keyPressed(em, SDL_SCANCODE_C) ||
        mouseClicked(em, SDL_BUTTON_RIGHT))
    {
        ui.active_screen = UIState::Screen::Sanctuary;
        playSfx(snd);
        return;
    }

    const entt::entity player = findPlayerEntity(em.registry());
    const Inventory* inv = (player != entt::null && em.registry().all_of<Inventory>(player))
                               ? &em.registry().get<Inventory>(player)
                               : nullptr;

    const std::vector<DisplayRow> displayRows = buildDisplayRows(recipes, items);
    std::vector<int> navOrder;
    for (const auto& row : displayRows)
        if (!row.is_header)
            navOrder.push_back(row.recipe_index);

    validateSelection(navOrder);
    sHoveredRecipe = -1;
    handleKeyboardNav(em, navOrder);

    bool canCraft = false;
    if (sSelectedRecipe >= 0 && recipe_count > 0 && inv != nullptr)
    {
        const auto& recipe = recipes.recipes[static_cast<size_t>(sSelectedRecipe)];
        canCraft = CraftingOps::canCraft(*inv, recipe, items);
    }
    canCraft = handleCraftAction(em, recipes, items, snd, inv, player, canCraft);

    // --- Measure content for auto-sizing ---
    const float pad = 24.0f;
    const float title_h = FontManager::lineHeight(sTitleFont);
    const float line_h = FontManager::lineHeight(sBodyFont) + 6.0f;
    const float sep_gap = 12.0f;
    const float header_gap = 4.0f;
    const float btn_h = 32.0f;

    const std::string titleText = "Crafting";
    const TextSize tsz = UIRenderer::measureText(sTitleFont, titleText);
    float content_w = tsz.width;

    float list_h = 0.0f;
    measureRecipeRows(displayRows, line_h, header_gap, content_w, list_h);

    float detail_h = 0.0f;
    bool anyHasRequirements = false;
    const float stat_line = FontManager::lineHeight(sBodyFont) + 4.0f;
    if (recipe_count > 0)
        measureAllRecipes(recipes, items, stat_line, line_h, content_w, detail_h,
                          anyHasRequirements);

    const std::string hintText = "[Esc] Back   [W/S] Navigate   [Enter] Craft";
    const TextSize hintsz = UIRenderer::measureText(sBodyFont, hintText);
    content_w = std::max(content_w, hintsz.width);
    content_w = std::max(content_w, 340.0f);
    const float panel_w = content_w + pad * 2.0f;

    const float header_h = pad + title_h + sep_gap + 1.0f + sep_gap;
    const float footer_h = btn_h + sep_gap + 1.0f + sep_gap + hintsz.height + pad;
    const float detail_section_h =
        (recipe_count > 0) ? (sep_gap + 1.0f + sep_gap + detail_h) : 0.0f;
    const float ideal_panel_h = header_h + list_h + detail_section_h + footer_h;
    const float max_panel_h = wh - 60.0f;
    const float panel_h = std::min(ideal_panel_h, max_panel_h);
    // Only the recipe list scrolls; detail + footer are pinned.
    const float visible_list_h = panel_h - header_h - detail_section_h - footer_h;
    const float max_scroll = std::max(0.0f, list_h - visible_list_h);

    if (em.mouse_wheel_y != 0)
        sScrollOffset -= static_cast<float>(em.mouse_wheel_y) * line_h;
    sScrollOffset = std::clamp(sScrollOffset, 0.0f, max_scroll);

    if (!navOrder.empty())
        handleAutoScroll(displayRows, visible_list_h, max_scroll, line_h, header_gap);

    const float px = (ww - panel_w) * 0.5f;
    const float py = (wh - panel_h) * 0.5f;
    const float cx = px + pad;
    const float cw = content_w;
    const float scroll_top = py + header_h;
    const float list_bottom = scroll_top + visible_list_h;
    const float scroll_bottom = py + panel_h - footer_h;

    int mouseX = 0;
    int mouseY = 0;
    SDL_GetMouseState(&mouseX, &mouseY);
    const float mx = static_cast<float>(mouseX);
    const float my = static_cast<float>(mouseY);

    // Use list_bottom for scroll clipping so only the recipe list scrolls.
    const CraftLayout lay{px, cx, cw, panel_w, line_h, sep_gap, scroll_top, list_bottom, mx, my};

    UIRenderer::drawRect(px, py, panel_w, panel_h, PANEL_BG);

    float y = py + pad;
    UIRenderer::drawText(sTitleFont, titleText, px + (panel_w - tsz.width) * 0.5f, y, TITLE_COLOR);
    y += title_h + sep_gap;

    UIRenderer::drawRect(cx, y, cw, 1.0f, SEPARATOR);
    y += 1.0f + sep_gap;

    // Scissor clip the recipe list only.
    UIRenderer::flush();
    glEnable(GL_SCISSOR_TEST);
    glScissor(static_cast<int>(px), static_cast<int>(wh - list_bottom), static_cast<int>(panel_w),
              static_cast<int>(list_bottom - scroll_top));
    y -= sScrollOffset;

    drawRecipeList(displayRows, em, lay, header_gap, y);

    UIRenderer::flush();
    glDisable(GL_SCISSOR_TEST);

    // Detail + footer render below the list, unclipped.
    y = list_bottom;
    const int displayRecipe = (sHoveredRecipe >= 0) ? sHoveredRecipe : sSelectedRecipe;
    if (displayRecipe >= 0 && recipe_count > 0)
    {
        const auto& recipe = recipes.recipes[static_cast<size_t>(displayRecipe)];
        const ItemDef* output_def = items.find(recipe.output_item);
        const std::string outputName =
            (output_def != nullptr) ? output_def->name : recipe.output_item;
        renderRecipeDetail(recipe, output_def, outputName, inv, player, em, lay, y);
    }

    const float footer_start = scroll_bottom;
    renderCraftFooter(em, player, canCraft, lay, footer_start, btn_h, hintText, hintsz);
}
