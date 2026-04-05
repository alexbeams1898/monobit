#include "renderers/AIRecorder.h"

#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "ecs/GameComponents.h"
#include "ecs/GameConfig.h"

#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace
{

struct AIRecord
{
    int frame;
    uint32_t entity_id;
    float pos_x, pos_y;
    float vel_dx, vel_dy;
    int ai_state; // 0=Idle, 1=Chase, 2=Attack
    float slot_angle;
    bool has_token;
    float flow_dx, flow_dy;
    float target_x, target_y;
    float player_x, player_y;
};

std::vector<AIRecord> sBuffer;
int sCapacity = 0;
int sWriteIndex = 0;
int sCount = 0;

bool holdsToken(const AttackTokenPool& pool, entt::entity entity)
{
    for (auto h : pool.holders)
        if (h == entity)
            return true;
    return false;
}

} // namespace

namespace AIRecorder
{

void init(int max_entities, float history_seconds)
{
    const int ticksPerSec = 60;
    sCapacity = static_cast<int>(history_seconds * static_cast<float>(ticksPerSec)) * max_entities;
    sBuffer.resize(static_cast<size_t>(sCapacity));
    sWriteIndex = 0;
    sCount = 0;
}

void tick(EntityManager& em, int frame_number)
{
    if (sCapacity == 0)
        return;

    // Find player position.
    float px = 0.0f;
    float py = 0.0f;
    for (auto e : em.registry().view<PlayerActions>())
    {
        if (em.registry().all_of<Transform>(e))
        {
            const auto& t = em.registry().get<Transform>(e);
            px = t.x;
            py = t.y;
        }
        break;
    }

    const auto& pool = em.registry().ctx().get<AttackTokenPool>();
    const auto& ff = em.flow_field;
    const auto& f = em.registry().ctx().get<FormulaConfig>();

    for (auto [entity, ai, transform, vel] :
         em.registry().view<AIController, Transform, Velocity>().each())
    {
        AIRecord rec{};
        rec.frame = frame_number;
        rec.entity_id = entt::to_integral(entity);
        rec.pos_x = transform.x;
        rec.pos_y = transform.y;
        rec.vel_dx = vel.dx;
        rec.vel_dy = vel.dy;
        rec.ai_state = static_cast<int>(ai.state);
        rec.slot_angle = ai.slot_angle;
        rec.has_token = holdsToken(pool, entity);
        rec.player_x = px;
        rec.player_y = py;

        // Flow field direction at entity's cell.
        const int col = static_cast<int>(transform.x / FlowField::CELL_SIZE);
        const int row = static_cast<int>(transform.y / FlowField::CELL_SIZE);
        if (col >= 0 && col < FlowField::COLS && row >= 0 && row < FlowField::ROWS)
        {
            rec.flow_dx = ff.cells[row][col].dx;
            rec.flow_dy = ff.cells[row][col].dy;
        }

        // Slot target position.
        if (ai.state == AIController::State::Attack && ai.slot_angle != AIController::NO_SLOT)
        {
            const bool hasT = holdsToken(pool, entity);
            const float radius =
                hasT ? ai.attack_radius : ai.attack_radius * f.combat_ai.wait_radius_mult;
            rec.target_x = px + std::cos(ai.slot_angle) * radius;
            rec.target_y = py + std::sin(ai.slot_angle) * radius;
        }
        else
        {
            rec.target_x = px;
            rec.target_y = py;
        }

        sBuffer[static_cast<size_t>(sWriteIndex)] = rec;
        sWriteIndex = (sWriteIndex + 1) % sCapacity;
        if (sCount < sCapacity)
            ++sCount;
    }
}

void dump()
{
    if (sCount == 0)
        return;

    try
    {
        std::filesystem::create_directories("debug");
    }
    catch (const std::exception& e)
    {
        std::cerr << "[AIRecorder] Cannot create debug/: " << e.what() << "\n";
        return;
    }

    // Filename with timestamp.
    const std::time_t now = std::time(nullptr);
    char timeBuf[64];
    std::strftime(timeBuf, sizeof(timeBuf), "%Y%m%d_%H%M%S", std::localtime(&now));
    const std::string path = std::string("debug/ai_dump_") + timeBuf + ".csv";

    std::ofstream file(path);
    if (!file.is_open())
    {
        std::cerr << "[AIRecorder] Cannot write " << path << "\n";
        return;
    }

    file << "frame,entity_id,pos_x,pos_y,vel_dx,vel_dy,ai_state,slot_angle,"
            "has_token,flow_dx,flow_dy,target_x,target_y,player_x,player_y\n";

    // Iterate from oldest to newest.
    const int start = (sCount < sCapacity) ? 0 : sWriteIndex;
    for (int i = 0; i < sCount; ++i)
    {
        const auto& r = sBuffer[static_cast<size_t>((start + i) % sCapacity)];
        file << r.frame << ',' << r.entity_id << ',' << r.pos_x << ',' << r.pos_y << ',' << r.vel_dx
             << ',' << r.vel_dy << ',' << r.ai_state << ',' << r.slot_angle << ','
             << (r.has_token ? 1 : 0) << ',' << r.flow_dx << ',' << r.flow_dy << ',' << r.target_x
             << ',' << r.target_y << ',' << r.player_x << ',' << r.player_y << '\n';
    }
}

} // namespace AIRecorder
