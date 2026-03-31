#include "renderers/DebugOverlay.h"

#include "Engine.h"
#include "UIRenderer.h"
#include "ecs/Components.h"
#include "ecs/GameComponents.h"
#include "ecs/GameConfig.h"

#include <cmath>
#include <string>

static FontHandle sFont = INVALID_FONT;
static bool sVisible = false;

static constexpr Color BG{0.0f, 0.0f, 0.0f, 0.6f};
static constexpr Color TEXT{0.3f, 1.0f, 0.3f, 1.0f};

static const char* phaseName(WaveState::Phase p)
{
    switch (p)
    {
    case WaveState::Phase::Idle:
        return "Idle";
    case WaveState::Phase::Spawning:
        return "Spawning";
    case WaveState::Phase::Active:
        return "Active";
    case WaveState::Phase::Cleared:
        return "Cleared";
    case WaveState::Phase::SafeRoom:
        return "SafeRoom";
    case WaveState::Phase::GameOver:
        return "GameOver";
    case WaveState::Phase::Complete:
        return "Complete";
    case WaveState::Phase::Transitioning:
        return "Transitioning";
    }
    return "?";
}

namespace DebugOverlay
{

void init(FontHandle body_font)
{
    sFont = body_font;
}

void toggle()
{
    sVisible = !sVisible;
}

bool isVisible()
{
    return sVisible;
}

void render(Engine& engine, EntityManager& em, int window_w, int window_h)
{
    if (!sVisible || sFont == INVALID_FONT)
        return;

    (void)window_w;
    (void)window_h;

    const int fps = static_cast<int>(std::lround(1.0 / engine.lastFrameTime()));
    const float frameMs = static_cast<float>(engine.lastFrameTime() * 1000.0);

    // Entity count.
    int entityCount = 0;
    for (auto e : em.registry().storage<entt::entity>())
    {
        (void)e;
        ++entityCount;
    }

    // Player position.
    float px = 0.0f;
    float py = 0.0f;
    for (auto [entity, actions, t] : em.registry().view<PlayerActions, Transform>().each())
    {
        px = t.x;
        py = t.y;
        break;
    }

    // Camera position.
    float camX = 0.0f;
    float camY = 0.0f;
    for (auto [entity, camera] : em.registry().view<Camera>().each())
    {
        if (camera.active)
        {
            camX = camera.x;
            camY = camera.y;
            break;
        }
    }

    // Wave info.
    const auto& ws = em.registry().ctx().get<WaveState>();

    // Build lines.
    std::string lines[5];
    lines[0] = "FPS: " + std::to_string(fps) + "  (" +
               std::to_string(static_cast<int>(frameMs * 10.0f) / 10) + " ms)";
    lines[1] = "Entities: " + std::to_string(entityCount);
    lines[2] = "Player: " + std::to_string(static_cast<int>(px)) + ", " +
               std::to_string(static_cast<int>(py));
    lines[3] = "Camera: " + std::to_string(static_cast<int>(camX)) + ", " +
               std::to_string(static_cast<int>(camY));
    lines[4] = "Wave: " + std::to_string(ws.current_wave) + "  " + phaseName(ws.phase);

    const float lineH = FontManager::lineHeight(sFont);
    const float pad = 6.0f;
    const float x = 10.0f;
    const float y = 120.0f;

    // Background.
    float maxW = 0.0f;
    for (const auto& line : lines)
    {
        TextSize sz = UIRenderer::measureText(sFont, line);
        if (sz.width > maxW)
            maxW = sz.width;
    }
    UIRenderer::drawRect(x - pad, y - pad, maxW + pad * 2.0f, lineH * 5.0f + pad * 2.0f, BG);

    // Text.
    for (int i = 0; i < 5; ++i)
        UIRenderer::drawText(sFont, lines[i], x, y + lineH * static_cast<float>(i), TEXT);
}

} // namespace DebugOverlay
