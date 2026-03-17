#include "systems/FlowFieldSystem.h"

#include "ecs/Components.h"

#include <cmath>
#include <queue>

void FlowFieldSystem::update(EntityManager& em)
{
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

    auto& ff = em.flowField;

    // Debounce: only rebuild after the player has been in the same cell for
    // STABILITY_FRAMES consecutive frames.
    //
    // Without this, rapid player oscillation across a cell boundary (e.g.
    // moving back and forth faster than one cell per few frames) causes the
    // flow field to flip directions every update. The enemy's blended velocity
    // chases the oscillating target and makes zero net progress toward the
    // player. With debouncing, the field stays at the last stable position
    // during oscillation and only updates when the player commits to a new cell.
    if (playerCol != ff.pendingCol || playerRow != ff.pendingRow)
    {
        ff.pendingCol = playerCol;
        ff.pendingRow = playerRow;
        ff.stableCount = 0;
    }
    ++ff.stableCount;

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
    if (ff.stableCount < FlowField::STABILITY_FRAMES)
        return;

    // Player has been stable in this cell long enough — check if BFS is
    // already current for this position and skip if so.
    if (playerCol == ff.lastPlayerCol && playerRow == ff.lastPlayerRow)
        return;

    ff.lastPlayerCol = playerCol;
    ff.lastPlayerRow = playerRow;

    // Reset all flow vectors.
    for (auto& row : ff.cells)
        for (auto& cell : row)
            cell = {};

    // Mark static solid cells as impassable (walls, etc.).
    // Dynamic entities (Velocity present) are not treated as obstacles —
    // enemies can overlap each other, only static geometry blocks movement.
    bool walls[FlowField::ROWS][FlowField::COLS]{};
    for (auto e : em.registry().view<Transform, Collider>())
    {
        if (em.registry().all_of<Velocity>(e))
            continue; // dynamic — not a wall
        const auto& col = em.registry().get<Collider>(e);
        if (!col.is_solid)
            continue;
        const auto& t = em.registry().get<Transform>(e);

        // Mark every cell the tile's bounding box overlaps.
        // Transform stores the CENTER of the entity (same as CollisionSystem
        // and RenderSystem), so the bounding box runs from
        // (t.x - width/2) to (t.x + width/2 - 1) on each axis.
        // A 32 px tile spans exactly 2 × 2 cells at CELL_SIZE=16.
        const int cStart = static_cast<int>((t.x - col.width * 0.5f) / FlowField::CELL_SIZE);
        const int cEnd = static_cast<int>((t.x + col.width * 0.5f - 1.0f) / FlowField::CELL_SIZE);
        const int rStart = static_cast<int>((t.y - col.height * 0.5f) / FlowField::CELL_SIZE);
        const int rEnd = static_cast<int>((t.y + col.height * 0.5f - 1.0f) / FlowField::CELL_SIZE);
        for (int rr = rStart; rr <= rEnd; ++rr)
            for (int cc = cStart; cc <= cEnd; ++cc)
                if (cc >= 0 && cc < FlowField::COLS && rr >= 0 && rr < FlowField::ROWS)
                    walls[rr][cc] = true;
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
    bool clearance[FlowField::ROWS][FlowField::COLS]{};
    for (int r = 0; r < FlowField::ROWS; ++r)
    {
        for (int c = 0; c < FlowField::COLS; ++c)
        {
            if (walls[r][c])
                continue; // wall cells are already excluded from BFS
            // If any of the 8 neighbours (cardinal + diagonal) is a wall, this
            // cell is a clearance zone.  Cardinal neighbours catch cells beside
            // wall faces; diagonal neighbours catch cells near wall corners.
            // Without diagonal checking, a cell just outside a door jamb corner
            // passes the clearance test (no cardinal wall neighbour) but still
            // lets a 32 px entity clip the corner — MovementSystem blocks it and
            // the entity freezes.  8-way adjacency is symmetric: the same cell
            // that is clearance approaching from the south is also clearance
            // approaching from the north, east, or west.
            static constexpr int CDIRS[8][2] = {
                {-1, 0},  {1, 0},  {0, -1}, {0, 1}, // cardinal
                {-1, -1}, {-1, 1}, {1, -1}, {1, 1}  // diagonal
            };
            for (const auto& cd : CDIRS)
            {
                const int nr = r + cd[1];
                const int nc = c + cd[0];
                if (nr >= 0 && nr < FlowField::ROWS && nc >= 0 && nc < FlowField::COLS &&
                    walls[nr][nc])
                {
                    clearance[r][c] = true;
                    break;
                }
            }
        }
    }

    // BFS outward from the player's cell.
    // Each cell stores the direction back toward its BFS parent — one step
    // closer to the player along the shortest open path.
    // Clearance zones are excluded so paths never run along wall edges.
    bool visited[FlowField::ROWS][FlowField::COLS]{};

    struct Item
    {
        int col, row;
        int parentCol, parentRow;
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
