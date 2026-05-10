#include "systems/FlowFieldSystem.h"

#include "TileMap.h"
#include "ecs/Components.h"

#include <tracy/Tracy.hpp>

#include <cmath>
#include <queue>

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void FlowFieldSystem::update(EntityManager& em, float targetX, float targetY)
{
    ZoneScopedN("FlowFieldSystem");

    const int targetCol = static_cast<int>(targetX / FlowField::CELL_SIZE);
    const int targetRow = static_cast<int>(targetY / FlowField::CELL_SIZE);

    auto& ff = em.flow_field;

    if (targetCol != ff.pending_col || targetRow != ff.pending_row)
    {
        ff.pending_col = targetCol;
        ff.pending_row = targetRow;
        ff.stable_count = 0;
    }
    ++ff.stable_count;

    // Density binning -- count all NavAgent entities per cell.
    // Stopped entities are included so other systems (orbit gating,
    // crowd repulsion) can detect stacking even at zero velocity.
    for (auto& row : ff.density)
        for (auto& cell : row)
            cell = 0;

    for (auto [e, nav, t] : em.registry().view<NavAgent, Transform>().each())
    {
        const int ec = static_cast<int>(t.x / FlowField::CELL_SIZE);
        const int er = static_cast<int>(t.y / FlowField::CELL_SIZE);
        if (ec >= 0 && ec < FlowField::COLS && er >= 0 && er < FlowField::ROWS)
            if (ff.density[er][ec] < 255)
                ++ff.density[er][ec];
    }

    if (ff.stable_count < FlowField::STABILITY_FRAMES)
        return;

    if (targetCol == ff.last_player_col && targetRow == ff.last_player_row)
        return;

    ff.last_player_col = targetCol;
    ff.last_player_row = targetRow;

    for (auto& row : ff.cells)
        for (auto& cell : row)
            cell = {};

    bool walls[FlowField::ROWS][FlowField::COLS]{};

    auto markWallTile = [&](int col, int row)
    {
        const int c0 =
            static_cast<int>(static_cast<float>(col * TileMap::TILE_SIZE) / FlowField::CELL_SIZE);
        const int r0 =
            static_cast<int>(static_cast<float>(row * TileMap::TILE_SIZE) / FlowField::CELL_SIZE);
        const int c1 = c0 + 1;
        const int r1 = r0 + 1;
        if (r0 < FlowField::ROWS && c0 < FlowField::COLS)
            walls[r0][c0] = true;
        if (r0 < FlowField::ROWS && c1 < FlowField::COLS)
            walls[r0][c1] = true;
        if (r1 < FlowField::ROWS && c0 < FlowField::COLS)
            walls[r1][c0] = true;
        if (r1 < FlowField::ROWS && c1 < FlowField::COLS)
            walls[r1][c1] = true;
    };

    if (em.tile_map.valid())
    {
        for (int row = 0; row < em.tile_map.height; ++row)
            for (int col = 0; col < em.tile_map.width; ++col)
                if (!em.tile_map.at(col, row).walkable)
                    markWallTile(col, row);
    }
    else
    {
        for (auto e : em.registry().view<Transform, Collider>())
        {
            if (em.registry().all_of<Velocity>(e))
                continue;
            const auto& col_comp = em.registry().get<Collider>(e);
            if (!col_comp.is_solid)
                continue;
            const auto& t = em.registry().get<Transform>(e);
            const int cStart =
                static_cast<int>((t.x - col_comp.width * 0.5f) / FlowField::CELL_SIZE);
            const int cEnd =
                static_cast<int>((t.x + col_comp.width * 0.5f - 1.0f) / FlowField::CELL_SIZE);
            const int rStart =
                static_cast<int>((t.y - col_comp.height * 0.5f) / FlowField::CELL_SIZE);
            const int rEnd =
                static_cast<int>((t.y + col_comp.height * 0.5f - 1.0f) / FlowField::CELL_SIZE);
            for (int rr = rStart; rr <= rEnd; ++rr)
                for (int cc = cStart; cc <= cEnd; ++cc)
                    if (cc >= 0 && cc < FlowField::COLS && rr >= 0 && rr < FlowField::ROWS)
                        walls[rr][cc] = true;
        }
    }

    // Clearance pass
    static constexpr int CDIRS[8][2] = {{-1, 0},  {1, 0},  {0, -1}, {0, 1},
                                        {-1, -1}, {-1, 1}, {1, -1}, {1, 1}};
    auto hasWallNeighbor = [&](int r, int c) -> bool
    {
        for (const auto& cd : CDIRS)
        {
            const int nr = r + cd[1];
            const int nc = c + cd[0];
            if (nr >= 0 && nr < FlowField::ROWS && nc >= 0 && nc < FlowField::COLS && walls[nr][nc])
                return true;
        }
        return false;
    };

    bool clearance[FlowField::ROWS][FlowField::COLS]{};
    for (int r = 0; r < FlowField::ROWS; ++r)
        for (int c = 0; c < FlowField::COLS; ++c)
            if (!walls[r][c] && hasWallNeighbor(r, c))
                clearance[r][c] = true;

    // BFS
    bool visited[FlowField::ROWS][FlowField::COLS]{};

    struct Item
    {
        int col, row;
        int parent_col, parent_row;
    };

    std::queue<Item> q;

    if (targetCol >= 0 && targetCol < FlowField::COLS && targetRow >= 0 &&
        targetRow < FlowField::ROWS)
    {
        visited[targetRow][targetCol] = true;
        q.push({targetCol, targetRow, targetCol, targetRow});
    }

    static constexpr int DIRS[8][2] = {{-1, 0},  {1, 0},  {0, -1}, {0, 1},
                                       {-1, -1}, {-1, 1}, {1, -1}, {1, 1}};

    while (!q.empty())
    {
        auto [col, row, pCol, pRow] = q.front();
        q.pop();

        const float ddx = static_cast<float>(pCol - col);
        const float ddy = static_cast<float>(pRow - row);
        const float len = std::sqrt(ddx * ddx + ddy * ddy);
        if (len > 0.0f)
            ff.cells[row][col] = {ddx / len, ddy / len};

        for (const auto& dir : DIRS)
        {
            const int nc = col + dir[0];
            const int nr = row + dir[1];
            if (nc < 0 || nc >= FlowField::COLS || nr < 0 || nr >= FlowField::ROWS)
                continue;
            if (visited[nr][nc] || walls[nr][nc] || clearance[nr][nc])
                continue;
            if (dir[0] != 0 && dir[1] != 0)
            {
                if (walls[row][nc] || clearance[row][nc] || walls[nr][col] || clearance[nr][col])
                    continue;
            }
            visited[nr][nc] = true;
            q.push({nc, nr, col, row});
        }
    }

    // Clearance fill pass
    {
        bool fillVis[FlowField::ROWS][FlowField::COLS]{};

        std::queue<Item> fillQ;
        for (int r = 0; r < FlowField::ROWS; ++r)
        {
            for (int c = 0; c < FlowField::COLS; ++c)
            {
                if (walls[r][c] || clearance[r][c])
                    continue;
                fillVis[r][c] = true;
                if (ff.cells[r][c].dx != 0.0f || ff.cells[r][c].dy != 0.0f)
                    fillQ.push({c, r, c, r});
            }
        }

        while (!fillQ.empty())
        {
            auto [col, row, pCol, pRow] = fillQ.front();
            fillQ.pop();

            for (const auto& dir : DIRS)
            {
                const int nc = col + dir[0];
                const int nr = row + dir[1];
                if (nc < 0 || nc >= FlowField::COLS || nr < 0 || nr >= FlowField::ROWS)
                    continue;
                if (fillVis[nr][nc] || walls[nr][nc])
                    continue;
                fillVis[nr][nc] = true;
                // Copy parent's BFS direction so clearance cells route
                // around obstacles instead of pointing toward the nearest
                // routable cell (which can aim into the obstacle).
                ff.cells[nr][nc] = ff.cells[row][col];
                fillQ.push({nc, nr, col, row});
            }
        }
    }
}
