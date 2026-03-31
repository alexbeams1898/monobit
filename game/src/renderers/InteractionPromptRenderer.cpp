#include "renderers/InteractionPromptRenderer.h"

#include "UIRenderer.h"
#include "ecs/Components.h"
#include "ecs/GameComponents.h"
#include "ecs/GameConfig.h"

#include <cmath>
#include <string>
#include <tracy/Tracy.hpp>

static FontHandle sFont = INVALID_FONT;

static constexpr Color BG_COLOR{0.0f, 0.0f, 0.0f, 0.6f};
static constexpr Color TEXT_COLOR{1.0f, 1.0f, 1.0f, 1.0f};
static constexpr float PADDING = 6.0f;
static constexpr float SPRITE_HALF = 16.0f; // half of 32x32 sprite
static constexpr float GAP = 8.0f;          // space between sprite top and prompt

static void drawPrompt(const std::string& prompt, float screen_x, float sprite_top)
{
    const TextSize sz = UIRenderer::measureText(sFont, prompt);
    const float text_x = screen_x - sz.width * 0.5f;
    const float text_y = sprite_top - GAP - sz.height - PADDING * 2.0f;

    UIRenderer::drawRect(text_x - PADDING, text_y - PADDING, sz.width + PADDING * 2.0f,
                         sz.height + PADDING * 2.0f, BG_COLOR);
    UIRenderer::drawText(sFont, prompt, text_x, text_y, TEXT_COLOR);
}

void InteractionPromptRenderer::init(FontHandle font)
{
    sFont = font;
}

void InteractionPromptRenderer::render(EntityManager& em, float cam_x, float cam_y, int window_w,
                                       int window_h)
{
    ZoneScopedN("InteractionPrompt");

    const float half_w = static_cast<float>(window_w) * 0.5f;
    const float half_h = static_cast<float>(window_h) * 0.5f;

    // Pickup prompt (highest priority).
    for (auto [player, actions, interact] :
         em.registry().view<PlayerActions, InteractTarget>().each())
    {
        if (interact.entity == entt::null || !em.registry().valid(interact.entity))
            break;
        if (!em.registry().all_of<Pickup, Transform>(interact.entity))
            break;

        const auto& pickup = em.registry().get<Pickup>(interact.entity);
        if (pickup.item.empty())
            break;

        const auto& items = em.registry().ctx().get<ItemRegistry>();
        const ItemDef* def = items.find(pickup.item.config_path);
        const std::string name = (def != nullptr) ? def->name : "item";

        const auto& t = em.registry().get<Transform>(interact.entity);
        drawPrompt("[F] " + name, t.x - cam_x + half_w, t.y - cam_y + half_h - SPRITE_HALF);
        return;
    }

    // Rest spot prompt (only when no pickup prompt and no screen is open).
    const auto& ui = em.registry().ctx().get<UIState>();
    if (ui.isScreenOpen())
        return;

    float playerX = 0.0f;
    float playerY = 0.0f;
    bool hasPlayer = false;
    for (auto [entity, actions, transform] : em.registry().view<PlayerActions, Transform>().each())
    {
        playerX = transform.x;
        playerY = transform.y;
        hasPlayer = true;
        break;
    }
    if (!hasPlayer)
        return;

    for (auto [entity, spot, transform] : em.registry().view<RestSpot, Transform>().each())
    {
        const float dx = transform.x - playerX;
        const float dy = transform.y - playerY;
        if (std::sqrt(dx * dx + dy * dy) <= spot.radius)
        {
            drawPrompt("[F] Sanctuary", transform.x - cam_x + half_w,
                       transform.y - cam_y + half_h - SPRITE_HALF);
            return;
        }
    }

    // Ladder prompt (only when not spawning).
    for (auto [entity, ladder, transform] : em.registry().view<Ladder, Transform>().each())
    {
        if (ladder.spawning)
            continue;
        const float dx = transform.x - playerX;
        const float dy = transform.y - playerY;
        if (std::sqrt(dx * dx + dy * dy) <= ladder.radius)
        {
            drawPrompt("[F] Ascend", transform.x - cam_x + half_w,
                       transform.y - cam_y + half_h - SPRITE_HALF);
            return;
        }
    }
}
