#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "ecs/GameComponents.h"
#include "systems/ChaseSystem.h"
#include "systems/FlowFieldSystem.h"
#include "test_helpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

// ---------------------------------------------------------------------------
// ChaseSystem / FlowFieldSystem tests — no window, no GPU, no SDL required.
//
// Since issue/8, ChaseSystem reads from the flow field built by FlowFieldSystem
// rather than computing a direct vector to the player. Each test runs
// FlowFieldSystem::update first so the field is populated correctly.
//
// The flow field uses 4-directional BFS, so diagonal approach directions are
// resolved as cardinal (L-shaped paths), not true diagonals. Tests are written
// to match this grid-aligned behavior.
// ---------------------------------------------------------------------------

// Helpers -- create a player entity (has PlayerActions) and an enemy entity.
static entt::entity makePlayer(EntityManager& em, float x, float y)
{
    auto e = em.create();
    em.registry().emplace<Transform>(e, Transform{x, y});
    em.registry().emplace<PlayerActions>(e);
    return e;
}

// turn_speed defaults to 0 (instant snap) so existing tests can assert exact
// velocity values without compensating for blending math.
// Speed is set via FormulaConfig.movement.base with dex_scale=0 so tests get exact pixel values
// without having to solve the log formula in reverse. Each call overwrites the shared formula --
// do not mix different speeds in the same test unless you only need directional assertions.
static entt::entity makeEnemy(EntityManager& em, float x, float y, float speed,
                              AIController::State state = AIController::State::Chase,
                              float turn_speed = 0.0f)
{
    auto& f = em.registry().ctx().get<FormulaConfig>();
    f.movement.base = speed;
    f.movement.dex_scale = 0.0f; // DEX has no effect in tests

    auto e = em.create();
    em.registry().emplace<Transform>(e, Transform{x, y});
    em.registry().emplace<Velocity>(e);
    em.registry().emplace<Stats>(e); // required by ChaseSystem for DEX lookup
    AIController ai;
    ai.state = state;
    ai.turn_speed = turn_speed;
    em.registry().emplace<AIController>(e, ai);
    em.registry().emplace<NavAgent>(e);
    return e;
}

static entt::entity makeWall(EntityManager& em, float x, float y)
{
    auto e = em.create();
    em.registry().emplace<Transform>(e, Transform{x, y});
    em.registry().emplace<Collider>(e, Collider{32.0f, 32.0f, true});
    // No Velocity — treated as static obstacle by FlowFieldSystem.
    return e;
}

// Helper: run the full AI pipeline for one frame.
// FlowFieldSystem::update is called STABILITY_FRAMES times so the debounce
// counter reaches the threshold and the BFS actually runs. In tests the
// player never moves, so calling it N times in a row is equivalent to
// the player sitting still for N consecutive game frames.
static void runAI(EntityManager& em, double dt = 1.0 / 60.0)
{
    float px = 0.0f, py = 0.0f;
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
    for (int i = 0; i < FlowField::STABILITY_FRAMES; ++i)
        FlowFieldSystem::update(em, px, py);
    ChaseSystem::update(em, dt);
}

TEST_CASE("ChaseSystem moves entity directly toward player on x-axis", "[chase]")
{
    EntityManager em;
    emplaceGameConfigs(em);
    // Player at (100,0) → cell (6,0). Enemy at (0,0) → cell (0,0).
    // No walls — BFS gives direction (1,0) at cell (0,0).
    makePlayer(em, 100.0f, 0.0f);
    auto enemy = makeEnemy(em, 0.0f, 0.0f, 100.0f);

    runAI(em);

    const auto& vel = em.registry().get<Velocity>(enemy);
    REQUIRE(vel.dx == Catch::Approx(100.0f));
    REQUIRE(vel.dy == Catch::Approx(0.0f));
}

TEST_CASE("ChaseSystem speed is preserved for cardinal approach", "[chase]")
{
    // Velocity magnitude should equal ai.speed for a clear line-of-sight path.
    EntityManager em;
    emplaceGameConfigs(em);
    makePlayer(em, 500.0f, 0.0f);                  // player cell (31,0)
    auto enemy = makeEnemy(em, 0.0f, 0.0f, 80.0f); // cell (0,0)

    runAI(em);

    const auto& vel = em.registry().get<Velocity>(enemy);
    const float mag = std::sqrt(vel.dx * vel.dx + vel.dy * vel.dy);
    REQUIRE(mag == Catch::Approx(80.0f));
}

TEST_CASE("ChaseSystem does not move idle entity", "[chase]")
{
    EntityManager em;
    emplaceGameConfigs(em);
    makePlayer(em, 100.0f, 0.0f);
    auto enemy = makeEnemy(em, 0.0f, 0.0f, 100.0f, AIController::State::Idle);

    // Pre-set a non-zero velocity to confirm it is left untouched.
    em.registry().get<Velocity>(enemy) = {5.0f, 5.0f};

    runAI(em);

    const auto& vel = em.registry().get<Velocity>(enemy);
    REQUIRE(vel.dx == Catch::Approx(5.0f));
    REQUIRE(vel.dy == Catch::Approx(5.0f));
}

TEST_CASE("ChaseSystem does nothing when no player exists", "[chase]")
{
    EntityManager em;
    emplaceGameConfigs(em);
    // No player entity (no PlayerActions) -- FlowFieldSystem skips rebuild,
    // all cells remain (0,0), ChaseSystem writes zero velocity.
    auto enemy = makeEnemy(em, 0.0f, 0.0f, 100.0f);

    runAI(em);

    const auto& vel = em.registry().get<Velocity>(enemy);
    REQUIRE(vel.dx == Catch::Approx(0.0f));
    REQUIRE(vel.dy == Catch::Approx(0.0f));
}

TEST_CASE("ChaseSystem produces zero velocity when enemy is at player's cell", "[chase]")
{
    EntityManager em;
    emplaceGameConfigs(em);
    // Enemy on top of player — same grid cell, flow direction = (0,0).
    makePlayer(em, 0.0f, 0.0f);
    auto enemy = makeEnemy(em, 0.0f, 0.0f, 100.0f);

    runAI(em);

    const auto& vel = em.registry().get<Velocity>(enemy);
    REQUIRE(vel.dx == Catch::Approx(0.0f));
    REQUIRE(vel.dy == Catch::Approx(0.0f));
}

TEST_CASE("ChaseSystem handles multiple enemies independently", "[chase]")
{
    EntityManager em;
    emplaceGameConfigs(em);
    makePlayer(em, 0.0f, 0.0f); // player at cell (0,0)

    // Both enemies use the same formula base — test verifies direction independence, not magnitude.
    // Enemy A is directly to the right (cell 6,0) — flow points left (-1,0).
    auto enemyA = makeEnemy(em, 100.0f, 0.0f, 100.0f);
    // Enemy B is directly below (cell 0,12) — flow points up (0,-1).
    auto enemyB = makeEnemy(em, 0.0f, 200.0f, 100.0f);

    runAI(em);

    const auto& velA = em.registry().get<Velocity>(enemyA);
    REQUIRE(velA.dx < 0.0f); // moving left toward player
    REQUIRE(velA.dy == Catch::Approx(0.0f));

    const auto& velB = em.registry().get<Velocity>(enemyB);
    REQUIRE(velB.dx == Catch::Approx(0.0f));
    REQUIRE(velB.dy < 0.0f); // moving up toward player
}

TEST_CASE("FlowFieldSystem routes enemy around a wall", "[chase][flowfield]")
{
    EntityManager em;
    emplaceGameConfigs(em);
    // Transform stores CENTERS. Wall center (32,0) 32x32 spans x=16..47, y=-16..15.
    // With CELL_SIZE=16, marked cells: col=1..2 (x=16..47), row=0 (y=0..15 only —
    // the top half at y<0 is out of bounds and skipped).
    //
    // Layout (each unit = one 16px cell):
    //
    //   col:  0    1    2    3    4
    //  row 0: [E]  [W] [W]       [P]
    //
    // Player at cell (4,0), enemy at cell (0,0).
    // Direct path along row 0 blocked. BFS routes via row 1:
    //   (4,0)→(3,0)→(3,1)→(2,1)→(1,1)→(0,1)→(0,0)
    // Cell (0,0) parent is (0,1) → direction (0,+1) = move down.
    makePlayer(em, 64.0f, 0.0f); // center (64,0) → cell (4,0)
    makeWall(em, 32.0f, 0.0f);   // center (32,0) 32x32 → blocks cells (1-2, row 0)
    auto enemy = makeEnemy(em, 0.0f, 0.0f, 80.0f); // center (0,0) → cell (0,0)

    runAI(em);

    const auto& vel = em.registry().get<Velocity>(enemy);

    // Enemy must NOT head directly right (that path is blocked by the wall).
    // Expected: routed downward to go around the wall.
    REQUIRE(vel.dx == Catch::Approx(0.0f));
    REQUIRE(vel.dy == Catch::Approx(80.0f));
}

TEST_CASE("ChaseSystem velocity blending converges toward target over multiple frames",
          "[chase][blending]")
{
    // turn_speed > 0 blends velocity toward the flow-field direction each frame.
    // After one frame the velocity should be strictly between 0 and the target,
    // and successive frames must move monotonically closer to the target.
    EntityManager em;
    emplaceGameConfigs(em);
    // Player far to the right — clean cardinal path, no walls.
    makePlayer(em, 500.0f, 0.0f);
    constexpr float SPEED = 80.0f;
    constexpr float TURN_SPEED = 8.0f;
    constexpr double DT = 1.0 / 60.0;
    auto enemy = makeEnemy(em, 0.0f, 0.0f, SPEED, AIController::State::Chase, TURN_SPEED);

    // Frame 0 → Frame 1.
    runAI(em, DT);
    const float vel1 = em.registry().get<Velocity>(enemy).dx;
    REQUIRE(vel1 > 0.0f);  // must have started moving
    REQUIRE(vel1 < SPEED); // must NOT have snapped instantly to full speed

    // Frame 1 → Frame 2.
    runAI(em, DT);
    const float vel2 = em.registry().get<Velocity>(enemy).dx;
    REQUIRE(vel2 > vel1); // velocity must increase monotonically

    // Frame 2 → Frame 3.
    runAI(em, DT);
    const float vel3 = em.registry().get<Velocity>(enemy).dx;
    REQUIRE(vel3 > vel2);
}

TEST_CASE("ChaseSystem uses flow field at long range, not direct vector", "[chase][blend]")
{
    // Regression: a previous version blended in the direct vector at long range
    // (dist > 320 px, directWeight → 1.0). This caused enemies to charge through
    // walls when the player was on the other side.
    //
    // ChaseSystem now uses the flow field exclusively. In open space the flow field
    // points straight at the player (same result as direct vector for cardinal paths),
    // but unlike the direct vector it respects wall geometry.
    //
    // Player due east at 500 px (> old FAR threshold of 320). No walls.
    // BFS routes east from enemy → flow field direction = (+1,0) → vel.dx = speed.
    EntityManager em;
    emplaceGameConfigs(em);
    constexpr float SPEED = 80.0f;
    makePlayer(em, 500.0f, 0.0f);
    auto enemy = makeEnemy(em, 0.0f, 0.0f, SPEED); // turn_speed=0

    runAI(em);

    const auto& vel = em.registry().get<Velocity>(enemy);
    REQUIRE(vel.dx == Catch::Approx(SPEED));
    REQUIRE(vel.dy == Catch::Approx(0.0f));
}

TEST_CASE("ChaseSystem respects flow field when direct vector is blocked by a wall",
          "[chase][blend]")
{
    // Regression: enemy inside room, player far to the east (> old DIRECT_CHASE_FAR).
    // Previously the direct vector was blended in at long range, overriding the
    // flow field. The flowDotDirect suppression only caught dot ≤ 0 cases —
    // near-perpendicular cases (e.g. flow=south, direct=east, dot=+0.14) still
    // fired the blend, sending the enemy into the wall. Fixed by removing the
    // direct-vector blend entirely: ChaseSystem now uses the flow field always.
    //
    // Setup (each unit = one 16 px cell):
    //
    //   col:  0    1    2    3    4  ...  32
    //  row 0: [E] [CL] [W] [W] [CL]  ...  [P]
    //  row 1:      [CL][CL]
    //
    //  [W]  = wall cells (32x32 center at (32,0) → cols 1-2, row 0)
    //  [CL] = clearance zone (excluded from BFS routing)
    //  [E]  = enemy start (0,0) — clearance cell filled south by fill pass
    //  [P]  = player (512,0)
    //
    // Flow field: BFS routes south then east around the wall.
    // Fill pass gives (0,0) direction south (0,+1) → vel.dy = +speed.
    EntityManager em;
    emplaceGameConfigs(em);
    constexpr float SPEED = 80.0f;
    makePlayer(em, 512.0f, 0.0f);
    makeWall(em, 32.0f, 0.0f);                     // center (32,0) 32x32 → blocks cols 1-2, row 0
    auto enemy = makeEnemy(em, 0.0f, 0.0f, SPEED); // turn_speed=0

    runAI(em);

    const auto& vel = em.registry().get<Velocity>(enemy);
    REQUIRE(vel.dx == Catch::Approx(0.0f));
    REQUIRE(vel.dy == Catch::Approx(80.0f)); // routed south around the wall
}

TEST_CASE("FlowFieldSystem wall marking uses center-based coordinates, not top-left",
          "[chase][flowfield]")
{
    EntityManager em;
    emplaceGameConfigs(em);
    // Regression: wall marking must use Transform.x/y as CENTERS (matching
    // CollisionSystem and RenderSystem), not as top-left corners.
    //
    // Wall center (48,16) 32x32 spans x=32..63, y=0..31.
    // CORRECT center-based marking: cells col=2..3 (x=32..63), rows=0..1 (y=0..31).
    // WRONG top-left marking: cells col=3..4 (x=48..79), rows=1..2 (y=16..47).
    //
    // Layout (each unit = one 16px cell, correct marking):
    //
    //   col:  0    1    2    3    4
    //  row 0:       [P] [W] [W]
    //  row 1:           [W] [W]
    //  row 2:
    //  row 3:           [E]
    //
    // Player center (16,0) → col=1, row=0.
    // Enemy center (48,48) → col=3, row=3.
    //
    // With correct marking (cols 2-3 blocked in rows 0-1):
    //   Cells (1,1),(1,2),(2,2),(3,2) are in the clearance zone (8-way adjacent to
    //   wall cells). BFS routes left: (1,0)→(0,0)→(0,1)→(0,2)→(0,3)→(1,3)→(2,3)→(3,3).
    //   Cell (3,3) parent: (2,3). Direction: (-1,0) = move WEST.
    //
    // With wrong top-left marking (cols 3-4 blocked in rows 1-2):
    //   Cell (3,3) is adjacent to the misplaced walls (3,2)+(4,2) → clearance zone.
    //   Clearance fill gives it direction toward nearest routable cell (3,4) = SOUTH.
    //   The REQUIRE below (vel.dx == -60) catches this mismatch.
    makePlayer(em, 16.0f, 0.0f);
    makeWall(em, 48.0f, 16.0f);                      // center (48,16) 32x32 → cells (2-3, 0-1)
    auto enemy = makeEnemy(em, 48.0f, 48.0f, 60.0f); // center (48,48) → cell (3,3)

    runAI(em);

    const auto& vel = em.registry().get<Velocity>(enemy);
    REQUIRE(vel.dx == Catch::Approx(-60.0f)); // moving west — correct path around the wall
    REQUIRE(vel.dy == Catch::Approx(0.0f));
}

// ---------------------------------------------------------------------------
// Arrival softening tests
// ---------------------------------------------------------------------------

TEST_CASE("ChaseSystem arrival softening scales speed when within arrival_radius",
          "[chase][arrival]")
{
    // Player at (0,0). Enemy at (100,0) — dist=100, arrival_radius=200.
    // maxSpeed = 100 * (100/200) = 50.  Cell (6,0) → flow direction = (-1,0).
    // Blending (turn_speed=0) snaps vel to full speed (-100,0), then the
    // post-blend cap reduces it to 50: vel.dx = -50.
    EntityManager em;
    emplaceGameConfigs(em);
    constexpr float SPEED = 100.0f;
    makePlayer(em, 0.0f, 0.0f);
    auto enemy = makeEnemy(em, 100.0f, 0.0f, SPEED);

    auto& ai = em.registry().get<AIController>(enemy);
    ai.arrival_radius = 200.0f;
    em.registry().replace<AIController>(enemy, ai);

    runAI(em);

    const auto& vel = em.registry().get<Velocity>(enemy);
    REQUIRE(vel.dx == Catch::Approx(-50.0f)); // half speed — softened
    REQUIRE(vel.dy == Catch::Approx(0.0f));
}

TEST_CASE("ChaseSystem no arrival softening when arrival_radius is zero", "[chase][arrival]")
{
    EntityManager em;
    emplaceGameConfigs(em);
    constexpr float SPEED = 100.0f;
    makePlayer(em, 0.0f, 0.0f);
    auto enemy = makeEnemy(em, 100.0f, 0.0f, SPEED); // arrival_radius defaults to 0

    runAI(em);

    const auto& vel = em.registry().get<Velocity>(enemy);
    REQUIRE(vel.dx == Catch::Approx(-100.0f)); // full speed — no softening
    REQUIRE(vel.dy == Catch::Approx(0.0f));
}

TEST_CASE("ChaseSystem no arrival softening when enemy is beyond arrival_radius",
          "[chase][arrival]")
{
    // Enemy at (300,0) — dist=300 > arrival_radius=200. No softening.
    EntityManager em;
    emplaceGameConfigs(em);
    constexpr float SPEED = 100.0f;
    makePlayer(em, 0.0f, 0.0f);
    auto enemy = makeEnemy(em, 300.0f, 0.0f, SPEED);

    auto& ai = em.registry().get<AIController>(enemy);
    ai.arrival_radius = 200.0f;
    em.registry().replace<AIController>(enemy, ai);

    runAI(em);

    const auto& vel = em.registry().get<Velocity>(enemy);
    REQUIRE(vel.dx == Catch::Approx(-100.0f)); // full speed
    REQUIRE(vel.dy == Catch::Approx(0.0f));
}

// ---------------------------------------------------------------------------
// Attack state (battle formation) tests
// ---------------------------------------------------------------------------

TEST_CASE("ChaseSystem Attack state moves enemy toward slot on player ring", "[chase][attack]")
{
    // Player at (0,0). Enemy in Attack at (200,0), attack_radius=48.
    // Enemy is directly east of player: slot = (0 + 1*48, 0) = (48,0).
    // slotDist = 200-48 = 152 >> CELL_SIZE. scale = min(1, 152/48) = 1.
    // targetDx = -speed. With turn_speed=0: vel.dx = -speed.
    EntityManager em;
    emplaceGameConfigs(em);
    makePlayer(em, 0.0f, 0.0f);
    auto enemy = makeEnemy(em, 200.0f, 0.0f, 100.0f, AIController::State::Attack);
    em.registry().patch<AIController>(enemy, [](AIController& ai) { ai.attack_radius = 48.0f; });

    runAI(em);

    const auto& vel = em.registry().get<Velocity>(enemy);
    REQUIRE(vel.dx < 0.0f); // moving left toward slot
    REQUIRE(vel.dy == Catch::Approx(0.0f));
}

TEST_CASE("ChaseSystem Attack state stops when at slot", "[chase][attack]")
{
    // Enemy already on the ring: dist to slot = 0.
    EntityManager em;
    emplaceGameConfigs(em);
    makePlayer(em, 0.0f, 0.0f);
    auto enemy = makeEnemy(em, 48.0f, 0.0f, 100.0f, AIController::State::Attack);
    em.registry().patch<AIController>(enemy, [](AIController& ai) { ai.attack_radius = 48.0f; });

    runAI(em);

    const auto& vel = em.registry().get<Velocity>(enemy);
    REQUIRE(vel.dx == Catch::Approx(0.0f));
    REQUIRE(vel.dy == Catch::Approx(0.0f));
}

TEST_CASE("ChaseSystem Attack state: enemies from different directions target different slots",
          "[chase][attack]")
{
    // Two enemies in Attack from opposite sides — they should move toward
    // opposing points on the ring, not the same spot.
    // Enemy A east of player (200,0) → slot (48,0) → moves left.
    // Enemy B south of player (0,200) → slot (0,48) → moves up.
    EntityManager em;
    emplaceGameConfigs(em);
    makePlayer(em, 0.0f, 0.0f);
    auto enemyA = makeEnemy(em, 200.0f, 0.0f, 100.0f, AIController::State::Attack);
    em.registry().patch<AIController>(enemyA, [](AIController& ai) { ai.attack_radius = 48.0f; });
    auto enemyB = makeEnemy(em, 0.0f, 200.0f, 100.0f, AIController::State::Attack);
    em.registry().patch<AIController>(enemyB, [](AIController& ai) { ai.attack_radius = 48.0f; });

    runAI(em);

    const auto& velA = em.registry().get<Velocity>(enemyA);
    const auto& velB = em.registry().get<Velocity>(enemyB);
    REQUIRE(velA.dx < 0.0f); // A moves left toward (48,0)
    REQUIRE(velA.dy == Catch::Approx(0.0f));
    REQUIRE(velB.dx == Catch::Approx(0.0f));
    REQUIRE(velB.dy < 0.0f); // B moves up toward (0,48)
}
