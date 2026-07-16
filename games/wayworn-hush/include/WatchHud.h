#pragma once

#include "UIRenderer.h" // Color
#include "WorldClock.h"

#include <string>

using FontHandle = int;

// The watch readout -- a small corner badge showing what the pocket watch says, while the
// pilgrim is carrying one. Time passes whether or not he can read it; this is him reading it
// (see docs/design/NOTEBOOK.md). Absent entirely when the watch isn't in the satchel.
//
// The world's clock holds while the pause page is open, so this holds with it -- the badge
// only draws over a running world.
namespace watch_hud
{

// Feel/placement (config over constants). Tunable; edit config/watch_hud.json.
struct Config
{
    // Placement: fractions of the window (top-left origin). Sits above the stance badge by
    // default -- the two are the only persistent HUD, and they read as one corner.
    float x = 0.02f;
    float y = 0.86f;
    float pad = 0.008f;                      // inner padding, CANVAS FRACTION (x hud::scale -> px)
    Color text{0.86f, 0.84f, 0.72f, 1.0f};   // brass/paper -- kin to the notebook page
    Color panel{0.10f, 0.11f, 0.13f, 0.72f}; // badge backing (matches the stance badge)
};

// Load the feel from config/watch_hud.json (silent no-op -> defaults if missing).
void load(Config& cfg, const std::string& path);

// Set the font once after it's loaded (shares the HUD label font).
void init(FontHandle badge_font);

// Draw the readout (window space). `carried` false draws nothing -- no watch, no reading.
void render(const Config& cfg, const worldclock::WorldClock& clock, bool carried, int windowW,
            int windowH);

} // namespace watch_hud
