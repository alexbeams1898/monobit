#include "renderers/AIDebugOverlay.h"

#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "ecs/GameComponents.h"
#include "ecs/GameConfig.h"
#include "utils/DebugDraw.h"

#include <cmath>

// Mode: 0=off, 1=enemies+slots, 2=enemies+slots+flowfield.
static int sMode = 0;

static constexpr Color IDLE_COLOR{0.5f, 0.5f, 0.5f, 0.8f};
static constexpr Color CHASE_COLOR{1.0f, 1.0f, 0.0f, 0.8f};
static constexpr Color ATTACK_TOKEN_COLOR{1.0f, 0.2f, 0.2f, 0.8f};
static constexpr Color ATTACK_WAIT_COLOR{1.0f, 0.6f, 0.2f, 0.8f};
static constexpr Color SLOT_COLOR{0.0f, 1.0f, 1.0f, 0.6f};
static constexpr Color RADIUS_COLOR{1.0f, 1.0f, 1.0f, 0.3f};
static constexpr Color FLOW_COLOR{0.3f, 0.5f, 1.0f, 0.4f};

namespace
{

bool entityHoldsToken(const AttackTokenPool& pool, entt::entity entity)
{
    for (auto h : pool.holders)
        if (h == entity)
            return true;
    return false;
}

void renderEnemies(EntityManager& em)
{
    // Find player position.
    float px = 0.0f;
    float py = 0.0f;
    bool playerFound = false;
    for (auto e : em.registry().view<PlayerActions>())
    {
        if (em.registry().all_of<Transform>(e))
        {
            const auto& t = em.registry().get<Transform>(e);
            px = t.x;
            py = t.y;
            playerFound = true;
        }
        break;
    }

    if (!playerFound)
        return;

    const auto& pool = em.registry().ctx().get<AttackTokenPool>();
    const auto& f = em.registry().ctx().get<FormulaConfig>();

    // Player attack radius circle.
    float playerAtkRadius = 0.0f;
    for (auto [entity, ai] : em.registry().view<AIController>().each())
    {
        playerAtkRadius = ai.attack_radius;
        break;
    }
    if (playerAtkRadius > 0.0f)
        DebugDraw::circle(px, py, playerAtkRadius, RADIUS_COLOR);

    // Per-enemy visualization.
    for (auto [entity, ai, transform] :
         em.registry().view<AIController, Transform>().each())
    {
        const bool hasToken = entityHoldsToken(pool, entity);

        // Color by state.
        Color dotColor = IDLE_COLOR;
        if (ai.state == AIController::State::Chase)
            dotColor = CHASE_COLOR;
        else if (ai.state == AIController::State::Attack)
            dotColor = hasToken ? ATTACK_TOKEN_COLOR : ATTACK_WAIT_COLOR;

        // Enemy position dot.
        DebugDraw::dot(transform.x, transform.y, 6.0f, dotColor);

        // Slot target visualization (Attack state with assigned slot).
        if (ai.state == AIController::State::Attack && ai.slot_angle != AIController::NO_SLOT)
        {
            const float radius =
                hasToken ? ai.attack_radius
                         : ai.attack_radius * f.combat_ai.wait_radius_mult;
            const float slotX = px + std::cos(ai.slot_angle) * radius;
            const float slotY = py + std::sin(ai.slot_angle) * radius;

            // Slot target marker.
            DebugDraw::dot(slotX, slotY, 4.0f, SLOT_COLOR);

            // Line from enemy to its slot target.
            Color lineColor = dotColor;
            lineColor.a = 0.4f;
            DebugDraw::line(transform.x, transform.y, slotX, slotY, lineColor);
        }
    }
}

void renderFlowField(EntityManager& em)
{
    const auto& ff = em.flow_field;
    const float halfW = DebugDraw::halfW();
    const float halfH = DebugDraw::halfH();
    const float camX = DebugDraw::camX();
    const float camY = DebugDraw::camY();

    // Visible cell range (with 1-cell margin).
    const int minCol =
        std::max(0, static_cast<int>((camX - halfW) / FlowField::CELL_SIZE) - 1);
    const int maxCol =
        std::min(FlowField::COLS - 1, static_cast<int>((camX + halfW) / FlowField::CELL_SIZE) + 1);
    const int minRow =
        std::max(0, static_cast<int>((camY - halfH) / FlowField::CELL_SIZE) - 1);
    const int maxRow =
        std::min(FlowField::ROWS - 1, static_cast<int>((camY + halfH) / FlowField::CELL_SIZE) + 1);

    // Render every-other cell to stay within quad budget.
    for (int row = minRow; row <= maxRow; row += 2)
    {
        for (int col = minCol; col <= maxCol; col += 2)
        {
            const auto& cell = ff.cells[row][col];
            if (cell.dx == 0.0f && cell.dy == 0.0f)
                continue;

            const float cx = (static_cast<float>(col) + 0.5f) * FlowField::CELL_SIZE;
            const float cy = (static_cast<float>(row) + 0.5f) * FlowField::CELL_SIZE;

            // Direction tip offset.
            const float tipX = cx + cell.dx * FlowField::CELL_SIZE * 0.4f;
            const float tipY = cy + cell.dy * FlowField::CELL_SIZE * 0.4f;

            DebugDraw::dot(cx, cy, 2.0f, FLOW_COLOR);
            DebugDraw::dot(tipX, tipY, 2.0f, FLOW_COLOR);
        }
    }
}

} // namespace

namespace AIDebugOverlay
{

void toggle()
{
    sMode = (sMode + 1) % 3;
}

bool isVisible()
{
    return sMode > 0;
}

void render(EntityManager& em)
{
    if (sMode == 0)
        return;

    renderEnemies(em);

    if (sMode >= 2)
        renderFlowField(em);
}

} // namespace AIDebugOverlay
