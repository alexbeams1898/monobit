#include "screens/CraftingScreen.h"

#include "UIRenderer.h"
#include "ecs/Components.h"
#include "ecs/GameComponents.h"
#include "ecs/GameConfig.h"

#include <SDL.h>
#include <string>
#include <tracy/Tracy.hpp>

static FontHandle sBodyFont = INVALID_FONT;
static FontHandle sTitleFont = INVALID_FONT;
static int sSelectedRecipe = 0;

static constexpr Color OVERLAY{0.0f, 0.0f, 0.0f, 0.75f};
static constexpr Color TITLE_COLOR{1.0f, 0.85f, 0.3f, 1.0f};
static constexpr Color TEXT_WHITE{1.0f, 1.0f, 1.0f, 1.0f};
static constexpr Color TEXT_DIM{0.6f, 0.6f, 0.6f, 1.0f};
static constexpr Color HAVE_COLOR{0.3f, 0.9f, 0.3f, 1.0f};
static constexpr Color NEED_COLOR{0.9f, 0.3f, 0.3f, 1.0f};
static constexpr Color SELECTED_BG{0.3f, 0.3f, 0.5f, 0.6f};
static constexpr Color PANEL_BG{0.08f, 0.08f, 0.12f, 0.9f};

void CraftingScreen::init(FontHandle body_font, FontHandle title_font)
{
    sBodyFont = body_font;
    sTitleFont = title_font;
    sSelectedRecipe = 0;
}

void CraftingScreen::reset()
{
    sSelectedRecipe = 0;
}

// Count how many of a given item the player has in inventory.
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

void CraftingScreen::render(EntityManager& em, int window_w, int window_h)
{
    ZoneScopedN("CraftingScreen");

    const float ww = static_cast<float>(window_w);
    const float wh = static_cast<float>(window_h);

    const auto& recipes = em.registry().ctx().get<RecipeRegistry>();
    const auto& items = em.registry().ctx().get<ItemRegistry>();
    const int recipe_count = static_cast<int>(recipes.recipes.size());

    // Process navigation.
    for (int key : em.key_down_events)
    {
        if (key == SDL_SCANCODE_UP || key == SDL_SCANCODE_W)
            sSelectedRecipe = (sSelectedRecipe - 1 + recipe_count) % recipe_count;
        else if (key == SDL_SCANCODE_DOWN || key == SDL_SCANCODE_S)
            sSelectedRecipe = (sSelectedRecipe + 1) % recipe_count;
    }

    // Overlay.
    UIRenderer::drawRect(0.0f, 0.0f, ww, wh, OVERLAY);

    // Panel.
    const float panel_w = 400.0f;
    const float panel_h = 400.0f;
    const float panel_x = (ww - panel_w) * 0.5f;
    const float panel_y = (wh - panel_h) * 0.5f;
    UIRenderer::drawRect(panel_x, panel_y, panel_w, panel_h, PANEL_BG);

    UIRenderer::drawText(sTitleFont, "Crafting", panel_x + 16.0f, panel_y + 12.0f, TITLE_COLOR);

    if (recipe_count == 0)
    {
        UIRenderer::drawText(sBodyFont, "No recipes available.", panel_x + 16.0f, panel_y + 60.0f,
                             TEXT_DIM);
        return;
    }

    // Find player inventory.
    entt::entity player = entt::null;
    for (auto e : em.registry().view<PlayerActions>())
    {
        player = e;
        break;
    }
    const Inventory* inv = (player != entt::null && em.registry().all_of<Inventory>(player))
                               ? &em.registry().get<Inventory>(player)
                               : nullptr;

    // Recipe list.
    float ry = panel_y + 48.0f;
    const float line_h = FontManager::lineHeight(sBodyFont) + 4.0f;

    for (int i = 0; i < recipe_count; ++i)
    {
        const auto& recipe = recipes.recipes[static_cast<size_t>(i)];
        const bool selected = (i == sSelectedRecipe);

        if (selected)
        {
            UIRenderer::drawRect(panel_x + 8.0f, ry - 2.0f, panel_w - 16.0f, line_h, SELECTED_BG);
        }

        UIRenderer::drawText(sBodyFont, recipe.name, panel_x + 16.0f, ry,
                             selected ? TEXT_WHITE : TEXT_DIM);
        ry += line_h;
    }

    // Selected recipe detail.
    if (sSelectedRecipe < recipe_count)
    {
        const auto& recipe = recipes.recipes[static_cast<size_t>(sSelectedRecipe)];
        float dy = panel_y + panel_h - 140.0f;

        // Output item.
        const ItemDef* output_def = items.find(recipe.output_item);
        const std::string output_name =
            (output_def != nullptr) ? output_def->name : recipe.output_item;
        UIRenderer::drawText(
            sBodyFont, "Result: " + output_name + " x" + std::to_string(recipe.output_quantity),
            panel_x + 16.0f, dy, TEXT_WHITE);
        dy += line_h;

        // Ingredients.
        UIRenderer::drawText(sBodyFont, "Requires:", panel_x + 16.0f, dy, TEXT_DIM);
        dy += line_h;

        for (const auto& ing : recipe.inputs)
        {
            const ItemDef* def = items.find(ing.config_path);
            const std::string name = (def != nullptr) ? def->name : ing.config_path;
            const int have = (inv != nullptr) ? countItem(*inv, ing.config_path) : 0;
            const Color c = (have >= ing.quantity) ? HAVE_COLOR : NEED_COLOR;
            const std::string label =
                "  " + name + " " + std::to_string(have) + "/" + std::to_string(ing.quantity);
            UIRenderer::drawText(sBodyFont, label, panel_x + 16.0f, dy, c);
            dy += line_h;
        }
    }

    UIRenderer::drawText(sBodyFont, "[C] Close   [Arrows] Navigate   [Enter] Craft",
                         panel_x + 16.0f, panel_y + panel_h - 24.0f, TEXT_DIM);
}
