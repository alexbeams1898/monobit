#include "systems/FlowFieldSystem.h"

#include "TileMap.h"
#include "ecs/Components.h"

#include <cmath>
#include <queue>
#include <tracy/Tracy.hpp>

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void FlowFieldSystem::update(EntityManager& em)
{
    ZoneScopedN("FlowFieldSystem");
    // Locate the player — identified by the Input component tag.
    float px = 0.0f;
    float py = 0.0f;
    bool playerFound = false;

    for (auto e : em.registry().view<Input>())
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

    const int playerCol = static_cast<int>(px / FlowField::CELL_SIZE);
    const int playerRow = static_cast<int>(py / FlowField::CELL_SIZE);

    auto& ff = em.flow_field;

    // Debounce: only rebuild after the player has been in the same cell for
    // STABILITY_FRAMES consecutive frames.
    //
    // Without this, rapid player oscillation across a cell boundary (e.g.
    // moving back and forth faster than one cell per few frames) causes the
    // flow field to flip directions every update. The enemy's blended velocity
    // chases the oscillating target and makes zero net progress toward the
    // player. With debouncing, the field stays at the last stable position
    // during oscillation and only updates when the player commits to a new cell.
    if (playerCol != ff.pending_col || playerRow != ff.pending_row)
    {
        ff.pending_col = playerCol;
        ff.pending_row = playerRow;
        ff.stable_count = 0;
    }
    ++ff.stable_count;

    // ---------------------------------------------------------------------------
    // Density binning pass — always runs every frame, independent of BFS.
    //
    // Clears the density grid then bins all chasing enemies by their center cell.
    // SteeringSystem reads this each frame to compute crowd-pressure separation:
    // each enemy steers away from high-density neighbours, breaking up the glob.
    // O(n) — one pass over all enemies, O(1) write per enemy.
    // ---------------------------------------------------------------------------
    for (auto& row : ff.density)
        for (auto& cell : row)
            cell = 0;

    for (auto e : em.registry().view<AIController, Transform>())
    {
        const auto& ai = em.registry().get<AIController>(e);
        if (ai.state == AIController::State::Idle)
            continue;
        const auto& t = em.registry().get<Transform>(e);
        const int ec = static_cast<int>(t.x / FlowField::CELL_SIZE);
        const int er = static_cast<int>(t.y / FlowField::CELL_SIZE);
        if (ec >= 0 && ec < FlowField::COLS && er >= 0 && er < FlowField::ROWS)
            if (ff.density[er][ec] < 255)
                ++ff.density[er][ec];
    }

    // BFS rebuild: only when the player has been stable for STABILITY_FRAMES
    // consecutive frames and the current BFS is outdated.
    if (ff.stable_count < FlowField::STABILITY_FRAMES)
        return;

    // Player has been stable in this cell long enough — check if BFS is
    // already current for this position and skip if so.
    if (playerCol == ff.last_player_col && playerRow == ff.last_player_row)
        return;

    ff.last_player_col = playerCol;
    ff.last_player_row = playerRow;

    // Reset all flow vectors.
    for (auto& row : ff.cells)
        for (auto& cell : row)
            cell = {};

    // Mark static solid cells as impassable.
    //
    // Tile map path: iterate non-walkable tiles and convert each to its 2x2
    // cell footprint. No ECS iteration needed — O(map_w * map_h) with zero
    // entity overhead.
    // ECS fallback: used by standalone tests that set up explicit wall entities
    // without a tile map (e.g. flow-field and chase system tests).
    bool walls[FlowField::ROWS][FlowField::COLS]{};

    // Each 32px tile maps to exactly 2x2 flow field cells at CELL_SIZE=16.
    // Unrolled — no inner loop needed.
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
        // ECS fallback — dynamic entities (Velocity present) are not obstacles.
        for (auto e : em.registry().view<Transform, Collider>())
        {
            if (em.registry().all_of<Velocity>(e))
                continue;
            const auto& col_comp = em.registry().get<Collider>(e);
            if (!col_comp.is_solid)
                continue;
            const auto& t = em.registry().get<Transform>(e);
            // Transform stores the CENTER; bounding box runs from
            // (t.x - width/2) to (t.x + width/2 - 1). A 32 px tile = 2x2 cells.
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

    // ---------------------------------------------------------------------------
    // Clearance pass — C-space (configuration space) expansion.
    //
    // Problem: BFS shortest paths run along wall edges. A 32 px entity routed
    // through a cell adjacent to a wall has its body (half-width ≈ 15 px) extend
    // into the wall, causing MovementSystem to zero its velocity → entity freezes.
    // This affects door edges, exterior corners, and any tight corridor.
    //
    // Fix: mark every open cell that touches a wall cell as routing-impassable
    // ("clearance zone"). The BFS then only places paths through cells whose
    // centers are at least CELL_SIZE (16 px) from any wall edge — guaranteed
    // clearance for a 32 px entity regardless of approach angle.
    //
    // Guarantee: CELL_SIZE (16) > entity half-width (15 with MOVEMENT_INSET=2),
    // so 1-cell clearance is always sufficient for 32 px entities.
    //
    // Minimum corridor width requirement: any opening must be ≥ 3 cells wide
    // (48 px) so that after removing 1 clearance cell on each side, at least
    // 1 center cell remains passable. The door is 4 tiles = 128 px = 8 cells,
    // leaving 6 routable center cells — far above the minimum.
    // ---------------------------------------------------------------------------
    // Returns true if any of the 8 neighbours (cardinal + diagonal) of (r,c) is a wall.
    // Cardinal neighbours catch cells beside wall faces; diagonal neighbours catch cells
    // near wall corners — without diagonal, a cell just outside a door jamb corner passes
    // the 4-way test but still clips a 32px entity → MovementSystem freezes it.
    static constexpr int CDIRS[8][2] = {
        {-1, 0},  {1, 0},  {0, -1}, {0, 1}, // cardinal
        {-1, -1}, {-1, 1}, {1, -1}, {1, 1}  // diagonal
    };
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

    // BFS outward from the player's cell.
    // Each cell stores the direction back toward its BFS parent — one step
    // closer to the player along the shortest open path.
    // Clearance zones are excluded so paths never run along wall edges.
    bool visited[FlowField::ROWS][FlowField::COLS]{};

    struct Item
    {
        int col, row;
        int parent_col, parent_row;
    };

    std::queue<Item> q;

    if (playerCol >= 0 && playerCol < FlowField::COLS && playerRow >= 0 &&
        playerRow < FlowField::ROWS)
    {
        // Seed the player's cell unconditionally — even if it's in a clearance
        // zone (player pressed against a wall). Enemies route toward it anyway.
        visited[playerRow][playerCol] = true;
        q.push({playerCol, playerRow, playerCol, playerRow});
    }

    // 8-directional expansion — cardinal + diagonal.
    //
    // Cardinal-only BFS was required when clearance zones were 4-way: a diagonal
    // BFS step could cross the corner between two wall-adjacent cells, routing
    // the entity through a wall edge.  Now that clearance is 8-way, every
    // routable cell is guaranteed to have no wall within 1 diagonal step, so
    // diagonal BFS steps through routable cells are safe.
    //
    // The diagonal corner-cutting guard below still applies to clearance cells:
    // if either cardinal intermediary of a diagonal step is clearance (wall-
    // adjacent), the diagonal is skipped — the entity would clip that wall edge.
    static constexpr int DIRS[8][2] = {
        {-1, 0},  {1, 0},  {0, -1}, {0, 1}, // cardinal
        {-1, -1}, {-1, 1}, {1, -1}, {1, 1}  // diagonal
    };

    while (!q.empty())
    {
        auto [col, row, pCol, pRow] = q.front();
        q.pop();

        // Direction from this cell toward its BFS parent = toward the player.
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
            // Diagonal corner-cutting guard: skip this step if either cardinal
            // intermediary is a wall or clearance zone.  Moving e.g. NE when N
            // or E is wall-adjacent would route the entity along the wall edge.
            if (dir[0] != 0 && dir[1] != 0)
            {
                if (walls[row][nc] || clearance[row][nc] || walls[nr][col] || clearance[nr][col])
                    continue;
            }
            visited[nr][nc] = true;
            q.push({nc, nr, col, row});
        }
    }

    // ---------------------------------------------------------------------------
    // Clearance fill pass — propagate routable directions into clearance cells.
    //
    // Problem: clearance cells (open cells adjacent to walls) were excluded from
    // BFS routing above. They have zero direction. ChaseSystem's direct-vector
    // fallback fires there, which can aim entities into walls — especially at
    // corners where two clearance zones overlap and the direct vector has no
    // nearby opening to aim through.
    //
    // Fix: flood-fill BFS from all routable cells outward into their clearance
    // neighbours, copying the nearest routable direction. An entity in a
    // clearance cell now gets a valid "toward player via nearest known-good path"
    // direction instead of "straight at player through whatever is in the way".
    //
    // This does NOT change routable-cell directions — only fills zeros in cells
    // that previously had none. MovementSystem + MOVEMENT_INSET still handle
    // any residual wall proximity.
    // ---------------------------------------------------------------------------
    {
        bool fillVis[FlowField::ROWS][FlowField::COLS]{};

        // Seed: every routable cell is already "visited" — the flood only
        // expands outward into clearance cells, never back into routable ones.
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
                // Point the clearance cell TOWARD its nearest routable ancestor,
                // not in the same direction as the ancestor.
                //
                // Why: copying the ancestor's direction (e.g. "north") into a
                // clearance cell adjacent to the door jamb sends the entity toward
                // the jamb wall, where MovementSystem blocks it. The entity needs
                // to steer back INTO the routable corridor first. Pointing toward
                // the ancestor achieves this: a cell east of the door gets "west"
                // (toward the routable door opening), not "north" (into the jamb).
                //
                // For deeper clearance pockets (corners where two zones overlap),
                // each clearance cell in the chain points to the next one inward,
                // forming a gradient that walks the entity back to routable space.
                const float ddx = static_cast<float>(col - nc);
                const float ddy = static_cast<float>(row - nr);
                const float len = std::sqrt(ddx * ddx + ddy * ddy);
                ff.cells[nr][nc] = {ddx / len, ddy / len};
                fillQ.push({nc, nr, nc, nr});
            }
        }
    }
}
