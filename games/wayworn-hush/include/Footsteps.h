#pragma once

#include <string>
#include <vector>

// Footstep SFX -- a speed-scaled cadence timer that plays a grass footfall as the
// pilgrim walks. Ported from prison-escape-game's timer approach (not selva's
// 3D skeleton foot-plant detection): while moving, a timer counts down and fires
// a step on zero, resetting to the walk / run cadence. One surface (grass) for
// now; the config shape holds a pool + volume so more surfaces drop in later.
// See docs/design/AESTHETIC.md (audio register).
namespace footsteps
{

// Authored tuning + the grass sound pool (config over constants).
struct Config
{
    std::vector<std::string> grass; // variation file paths, picked at random per step
    float volume = 0.4f;            // playback volume
    float walk_cadence = 0.42f;     // seconds between steps at walk speed
    float run_cadence = 0.28f;      // seconds between steps at run speed
};

// The per-player timer state (kept separate so the cadence is unit-testable).
struct State
{
    float step_timer = 0.0f;
};

// Load the pool + tuning from config/footsteps.json (silent no-op if missing).
void load(Config& cfg, const std::string& path);

// Advance the cadence timer by dt. Returns TRUE on the frame a step fires. Pure:
// no audio, so the cadence is testable. `moving` = the pilgrim is walking this
// frame; `run` selects the brisker cadence. A step fires immediately on the first
// moving frame (timer starts at 0), then every cadence seconds while moving.
bool tick(State& state, const Config& cfg, bool moving, bool run, float dt);

// Advance + play: ticks the timer and, on a step, plays a random grass variation.
void update(State& state, const Config& cfg, bool moving, bool run, float dt);

} // namespace footsteps
