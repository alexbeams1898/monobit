#pragma once

#include <string>
#include <vector>

class Engine;
class EntityManager;

// Moving between authored areas. One primitive -- load the target, place the
// player at the arrival door, cut -- with doors as the walk-on trigger and a
// black curtain over the swap.
//
// THE LINK RULE: a door names the DOOR it arrives at, never an area. Every
// door id is indexed to the file that declares it at scan time, so the
// destination is stated exactly once and a renamed file cannot leave a stale
// half of a link behind.
namespace travel
{

// Register this system's object builders ("door", "player_start") with the
// area loader. Once, at boot, before any area is entered.
void init();

// Index every door id across the project's levels. Safe to call on a missing
// file -- the game simply has no authored places yet.
void scan(const std::string& ldtkPath);

// Enter a level of the scanned project. `arriveAtDoor` empty means the
// level's player_start object. Transactional: a level that fails to load
// leaves the current world alone.
bool enter(Engine& engine, EntityManager& em, const std::string& level,
           const std::string& arriveAtDoor = {});

// Walk-on detection and the curtain clock. The swap happens here, at full
// black, at the top of an update -- never mid-tick while systems hold
// references into the registry.
void update(Engine& engine, EntityManager& em, float dt);

// The black over the swap, 0..1. Drawn by the shell above the HUD.
float curtainAlpha();

// True while a transition is in flight -- the world holds its breath.
bool active();

// The area the player is standing in; empty when the world is generated
// (a dug floor) rather than authored.
const std::string& currentArea();

// Every indexed door id, sorted -- for dev surfaces that offer jumps.
std::vector<std::string> doorIds();

// Dev jump: enter the area declaring `doorId`, arriving on that door --
// exactly the state walking through its counterpart would produce.
bool jumpToDoor(Engine& engine, EntityManager& em, const std::string& doorId);

// Forget area state (leaving the job for the title).
void reset();

// The swept-body test a door fires on: does the body, moving prev -> cur,
// cross the door's rect? Public because it is the one piece of this system
// worth pinning with tests.
bool sweptHit(float prevX, float prevY, float curX, float curY, float halfW, float halfH, float rx,
              float ry, float rw, float rh);

} // namespace travel
