#pragma once

#include <functional>
#include <string>
#include <vector>

class Engine;
class EntityManager;

// Moving between authored areas. One primitive -- load the target, place the
// player at the arrival warp, cut -- with warps as the walk-through trigger
// and a black curtain over the swap. A warp is a doorway, a staircase, a
// hole: the entity is the mechanism, the tiles under it are the look.
//
// THE LINK RULE: a warp names the WARP it arrives at, never a level. Every
// warp id is indexed to the level that declares it at scan time, so the
// destination is stated exactly once and a rename cannot leave a stale half
// of a link behind.
namespace travel
{

// Register this system's object builders ("warp", "player_start") with the
// area loader. Once, at boot, before any area is entered.
void init();

// Index every warp id across the project's levels. Safe to call on a missing
// file -- the game simply has no authored places yet.
void scan(const std::string& ldtkPath);

// Enter a level of the scanned project. `arriveAtWarp` empty means the
// level's player_start object. Transactional: a level that fails to load
// leaves the current world alone.
bool enter(Engine& engine, EntityManager& em, const std::string& level,
           const std::string& arriveAtWarp = {});

// Walk-on detection and the curtain clock. The swap happens here, at full
// black, at the top of an update -- never mid-tick while systems hold
// references into the registry.
void update(Engine& engine, EntityManager& em, float dt);

// The black over the swap, 0..1. Drawn by the shell above the HUD.
float curtainAlpha();

// True while a transition is in flight -- the world holds its breath.
// THE CURTAIN, for anything that swaps the world under him. The screen goes to black, `atBlack`
// runs where nothing shows, and the curtain lifts -- which is what a door already did, and what
// a way down and a way up should have been doing all along. One curtain in the game rather than
// one per system, so a cut cannot be shorter or darker depending on which act caused it.
void cut(std::function<void()> atBlack);

bool active();

// The area the player is standing in; empty when the world is generated
// (a dug floor) rather than authored.
const std::string& currentArea();

// Every indexed warp id, sorted -- for dev surfaces that offer jumps.
std::vector<std::string> warpIds();

// Dev jump: enter the level declaring `warpId`, arriving on that warp --
// exactly the state walking through its counterpart would produce.
bool jumpToWarp(Engine& engine, EntityManager& em, const std::string& warpId);

// Forget area state (leaving the job for the title).
void reset();

// Leaving authored space for a generated floor: the current level's warps and
// start stop existing (a stale warp rect firing under a dug chamber would be
// a wall teleporting him home), but the index and project stay scanned.
void leaveAuthored();

// The test a warp fires on: does the feet-centre, moving prev -> cur, cross
// the warp's EXIT LINE -- the edge opposite its facing -- moving outward?
// Walking into the strip does nothing; the cut lands at the moment you would
// emerge from its far side, so a passage is walked THROUGH. Swept, so a fast
// tick cannot step over the line. Public to pin with tests.
bool crossesExit(float prevX, float prevY, float curX, float curY, float rx, float ry, float rw,
                 float rh, const std::string& facing);

} // namespace travel
