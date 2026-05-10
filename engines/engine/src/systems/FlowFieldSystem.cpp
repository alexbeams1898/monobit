#include "systems/FlowFieldSystem.h"

#include "TileMap.h"
#include "ecs/Components.h"

#include <tracy/Tracy.hpp>

#include <cmath>
#include <queue>

namespace
{

// 8-direction grid step deltas (4-neighbor + diagonals). Used by the
// clearance pass and the BFS to walk neighbors.
constexpr int DIRS[8][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}, {-1, -1}, {-1, 1}, {1, -1}, {1, 1}};

using WallGrid = bool (&)[FlowField::ROWS][FlowField::COLS];

struct BfsItem
{
    int col;
    int row;
    int parent_col;
    int parent_row;
};

// Mark all 4 flow-field cells covered by one tilemap tile as wall.
// Tiles are 32px and cells are 16px, so each tile maps to a 2×2
// cell footprint.
void markWallTile(WallGrid walls, int col, int row)
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
}

// Mark walls from non-walkable tilemap cells.
void buildWallsFromTileMap(WallGrid walls, const TileMap& tile_map)
{
    for (int row = 0; row < tile_map.height; ++row)
        for (int col = 0; col < tile_map.width; ++col)
            if (!tile_map.at(col, row).walkable)
                markWallTile(walls, col, row);
}

// Mark walls from solid Collider entities (no Velocity = static).
// Used when no tile map is loaded (testing / placeholder worlds).
void buildWallsFromColliders(WallGrid walls, const EntityManager& em)
{
    for (auto e : em.registry().view<Transform, Collider>())
    {
        if (em.registry().all_of<Velocity>(e))
            continue;
        const auto& col_comp = em.registry().get<Collider>(e);
        if (!col_comp.is_solid)
            continue;
        const auto& t = em.registry().get<Transform>(e);
        const int cStart = static_cast<int>((t.x - col_comp.width * 0.5f) / FlowField::CELL_SIZE);
        const int cEnd =
            static_cast<int>((t.x + col_comp.width * 0.5f - 1.0f) / FlowField::CELL_SIZE);
        const int rStart = static_cast<int>((t.y - col_comp.height * 0.5f) / FlowField::CELL_SIZE);
        const int rEnd =
            static_cast<int>((t.y + col_comp.height * 0.5f - 1.0f) / FlowField::CELL_SIZE);
        for (int rr = rStart; rr <= rEnd; ++rr)
            for (int cc = cStart; cc <= cEnd; ++cc)
                if (cc >= 0 && cc < FlowField::COLS && rr >= 0 && rr < FlowField::ROWS)
                    walls[rr][cc] = true;
    }
}

// True if (r,c) has any wall neighbor in the 8-direction window.
bool hasWallNeighbor(WallGrid walls, int r, int c)
{
    for (const auto& cd : DIRS)
    {
        const int nr = r + cd[1];
        const int nc = c + cd[0];
        if (nr >= 0 && nr < FlowField::ROWS && nc >= 0 && nc < FlowField::COLS && walls[nr][nc])
            return true;
    }
    return false;
}

// Clearance pass: mark every non-wall cell adjacent to a wall as
// "clearance" so the main BFS routes through truly open cells. The
// fill pass then back-propagates the BFS direction into clearance
// cells so entities walking near walls get correct vectors.
void buildClearance(WallGrid walls, WallGrid clearance)
{
    for (int r = 0; r < FlowField::ROWS; ++r)
        for (int c = 0; c < FlowField::COLS; ++c)
            if (!walls[r][c] && hasWallNeighbor(walls, r, c))
                clearance[r][c] = true;
}

// Clear density bin and re-count NavAgent entities per cell.
void rebuildDensity(FlowField& ff, EntityManager& em)
{
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
}

// True if neighbor (nc, nr) reached from (col, row) via dir is a
// valid BFS step: in bounds, not visited / wall / clearance, and
// (for diagonals) both adjacent orthogonal cells are open (no
// corner-cutting around walls).
bool canBfsStepTo(WallGrid walls, WallGrid clearance,
                  bool (&visited)[FlowField::ROWS][FlowField::COLS], int col, int row, int nc,
                  int nr, const int (&dir)[2])
{
    if (nc < 0 || nc >= FlowField::COLS || nr < 0 || nr >= FlowField::ROWS)
        return false;
    if (visited[nr][nc] || walls[nr][nc] || clearance[nr][nc])
        return false;
    if (dir[0] != 0 && dir[1] != 0 &&
        (walls[row][nc] || clearance[row][nc] || walls[nr][col] || clearance[nr][col]))
        return false;
    return true;
}

// Write the per-cell direction vector pointing from (col, row)
// toward its BFS parent (pCol, pRow), normalized.
void writeBfsCellDirection(FlowField& ff, int col, int row, int pCol, int pRow)
{
    const float ddx = static_cast<float>(pCol - col);
    const float ddy = static_cast<float>(pRow - row);
    const float len = std::sqrt(ddx * ddx + ddy * ddy);
    if (len > 0.0f)
        ff.cells[row][col] = {ddx / len, ddy / len};
}

// Main BFS pass. Starts at the target cell and writes per-cell
// direction vectors pointing toward the parent. Diagonals require
// both adjacent orthogonal cells to be open (no corner-cutting).
void runFlowFieldBfs(FlowField& ff, WallGrid walls, WallGrid clearance, int targetCol,
                     int targetRow)
{
    bool visited[FlowField::ROWS][FlowField::COLS]{};
    std::queue<BfsItem> q;
    if (targetCol >= 0 && targetCol < FlowField::COLS && targetRow >= 0 &&
        targetRow < FlowField::ROWS)
    {
        visited[targetRow][targetCol] = true;
        q.push({targetCol, targetRow, targetCol, targetRow});
    }
    while (!q.empty())
    {
        auto [col, row, pCol, pRow] = q.front();
        q.pop();
        writeBfsCellDirection(ff, col, row, pCol, pRow);
        for (const auto& dir : DIRS)
        {
            const int nc = col + dir[0];
            const int nr = row + dir[1];
            if (!canBfsStepTo(walls, clearance, visited, col, row, nc, nr, dir))
                continue;
            visited[nr][nc] = true;
            q.push({nc, nr, col, row});
        }
    }
}

// Clearance fill pass: copy each routable cell's BFS direction into
// neighboring clearance cells, so wall-adjacent cells route around
// obstacles instead of aiming at the nearest open cell (which would
// often point into the wall).
void runClearanceFill(FlowField& ff, WallGrid walls, WallGrid clearance)
{
    bool fillVis[FlowField::ROWS][FlowField::COLS]{};
    std::queue<BfsItem> fillQ;
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
            ff.cells[nr][nc] = ff.cells[row][col];
            fillQ.push({nc, nr, col, row});
        }
    }
}

// Returns true if the field needs a full rebuild this frame: target
// cell stable for STABILITY_FRAMES AND target moved since the last
// successful build. Updates pending/stable bookkeeping as a side-
// effect.
bool shouldRebuildField(FlowField& ff, int targetCol, int targetRow)
{
    if (targetCol != ff.pending_col || targetRow != ff.pending_row)
    {
        ff.pending_col = targetCol;
        ff.pending_row = targetRow;
        ff.stable_count = 0;
    }
    ++ff.stable_count;
    if (ff.stable_count < FlowField::STABILITY_FRAMES)
        return false;
    if (targetCol == ff.last_player_col && targetRow == ff.last_player_row)
        return false;
    ff.last_player_col = targetCol;
    ff.last_player_row = targetRow;
    return true;
}

} // namespace

void FlowFieldSystem::update(EntityManager& em, float targetX, float targetY)
{
    ZoneScopedN("FlowFieldSystem");

    const int targetCol = static_cast<int>(targetX / FlowField::CELL_SIZE);
    const int targetRow = static_cast<int>(targetY / FlowField::CELL_SIZE);
    auto& ff = em.flow_field;

    rebuildDensity(ff, em);
    if (!shouldRebuildField(ff, targetCol, targetRow))
        return;

    for (auto& row : ff.cells)
        for (auto& cell : row)
            cell = {};

    bool walls[FlowField::ROWS][FlowField::COLS]{};
    if (em.tile_map.valid())
        buildWallsFromTileMap(walls, em.tile_map);
    else
        buildWallsFromColliders(walls, em);

    bool clearance[FlowField::ROWS][FlowField::COLS]{};
    buildClearance(walls, clearance);

    runFlowFieldBfs(ff, walls, clearance, targetCol, targetRow);
    runClearanceFill(ff, walls, clearance);
}
