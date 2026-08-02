#pragma once

#include <string>
#include <unordered_map>
#include <vector>

// Footstep SFX -- a speed-scaled cadence timer that plays a footfall matching the
// SURFACE the pilgrim is walking on. Ported from prison-escape-game's timer approach
// (not selva's 3D skeleton foot-plant detection): while moving, a timer counts down
// and fires a step on zero, resetting to the walk / run cadence. The sound pool is
// picked by surface name (the same LDtk surface tag that drives terrain collision),
// so grass, sand and bridge each sound distinct; a surface with no pool (water) is
// silent. See docs/design/AESTHETIC.md (audio register).
namespace footsteps
{

// Authored tuning + per-surface sound pools (config over constants). `pools` maps a
// surface name ("Grass", "Sand", "Bridge") to its footfall variations; a step picks a
// random file from the pool of the surface under the pilgrim's feet. A surface absent
// from `pools` plays nothing (water, until a swim/wade sound exists).
struct Config
{
    std::unordered_map<std::string, std::vector<std::string>> pools;
    std::string default_surface = "Grass"; // pool used when the surface is unknown/empty
    float volume = 0.4f;                   // playback volume
    float walk_cadence = 0.42f;            // seconds between steps at walk speed
    float run_cadence = 0.28f;             // seconds between steps at run speed
};

// The per-player timer state (kept separate so the cadence is unit-testable).
struct State
{
    float step_timer = 0.0f;
};

// Load the pools + tuning from config/footsteps.json (silent no-op if missing).
void load(Config& cfg, const std::string& path);

// The sound pool for `surface`, or nullptr when the step should be silent. An EMPTY
// surface (untagged tile) resolves to the default pool; a NAMED surface resolves only
// to its own pool -- if it has none, the step is silent by intent (water). Exposed so
// the surface->pool routing (the water-silent rule especially) is unit-testable.
const std::vector<std::string>* poolFor(const Config& cfg, const std::string& surface);

// Advance the cadence timer by dt. Returns TRUE on the frame a step fires. Pure:
// no audio, so the cadence is testable. `moving` = the pilgrim is walking this
// frame; `run` selects the brisker cadence. A step fires immediately on the first
// moving frame (timer starts at 0), then every cadence seconds while moving.
bool tick(State& state, const Config& cfg, bool moving, bool run, float dt);

// Advance + play: ticks the timer and, on a step, plays a random variation from the
// pool for `surface` (the tag under the pilgrim's feet). Empty/unknown surface -> the
// default pool; a surface with no pool (water) plays nothing.
void update(State& state, const Config& cfg, const std::string& surface, bool moving, bool run,
            float dt);

} // namespace footsteps
