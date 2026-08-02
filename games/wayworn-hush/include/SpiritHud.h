#pragma once

#include "UIRenderer.h" // Color

#include <string>

using FontHandle = int;

// The Spirit readout -- what he has to spend on becoming someone, in the corner, always.
//
// It behaves the way a souls counter behaves, because that IS the model: a gain announces
// itself as a small "+N" above the box and then FEEDS INTO the total, which climbs to meet it.
// The number you see is therefore not the number you have -- it is the number catching up, and
// the climb is the whole point (a big understanding is legible as a long climb rather than as a
// digit changing). No toast: the counter says it, once, in the place the total lives.
namespace spirit_hud
{

// Feel/placement (config over constants). Tunable; edit config/spirit_hud.json.
struct Config
{
    // Placement: fractions of the window (top-left origin). Sits in the corner opposite the
    // watch/stance pair -- what you HAVE, kept apart from what you can read.
    float x = 0.90f;
    float y = 0.90f;
    float pad = 0.008f; // inner padding, CANVAS FRACTION (x hud::scale -> px)
    // How fast the shown number climbs to the real one: a FRACTION of the remaining gap each
    // frame, so a large gain starts fast and eases in rather than crawling at a fixed rate.
    // `min_step` keeps the last few units from taking forever.
    float catch_up = 0.08f;
    int min_step = 1;
    float gain_hold = 1.1f;  // seconds the "+N" stays fully lit before it fades
    float gain_fade = 0.5f;  // seconds it takes to fade out
    float gain_rise = 0.02f; // how far the "+N" drifts upward as it fades (canvas fraction)
    Color text{0.92f, 0.88f, 0.70f, 1.0f};   // the total: warm, quiet
    Color gain{0.98f, 0.90f, 0.55f, 1.0f};   // the "+N": brighter, the moment of it
    Color panel{0.10f, 0.11f, 0.13f, 0.72f}; // badge backing (matches the other corner badges)
};

// Load the feel from config/spirit_hud.json (silent no-op -> defaults if missing).
void load(Config& cfg, const std::string& path);

// Set the font once after it's loaded (shares the HUD label font).
void init(FontHandle badge_font);

// Forget the climb and any pending "+N" -- called when a walk begins so the counter opens at
// the pilgrim's real total instead of climbing to it from whatever the last walk left on
// screen. An arrival is not a gain.
void reset(int spirit);

// Note that `amount` was just earned, so it can announce itself and be fed in. Called as each
// LINE surfaces, not when the engine banked it: two observations that each open a box are two
// moments, and each says its own "+N" as its box appears.
void gained(int amount);

// Advance the counter one frame, without drawing: the number shown climbs toward what has been
// announced. Split from render so the counting is pure and testable -- the bug it exists to
// prevent is invisible in a still frame.
//
// `spirit` is the pilgrim's TRUE total, which the engine credits in whole batches the instant
// it runs -- ahead of the lines that explain it. So the counter does NOT climb toward it
// directly: it climbs toward what has been ANNOUNCED (see gained), and reconciles to the truth
// only when `settled` says the queue has drained. That is what keeps a chain of gains feeding
// in one at a time instead of the box jumping to the sum while the labels trickle out behind
// it. `settled` = nothing waiting to be read (no pending lines, no box on screen).
void tick(const Config& cfg, int spirit, bool settled, float dt);

// What the counter is showing right now (after tick). Exposed for tests + debug readouts.
int shown();

// Where the badge sits on screen, in window px -- what a teaching card hones in on when it
// points here. Derived from the same numbers render() draws with, so the lit rectangle and the
// badge cannot drift apart. Zero-width if there is no font yet.
struct Rect
{
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
};
Rect bounds(const Config& cfg, int windowW, int windowH);

// Draw the readout (window space). Call tick() first; this only paints what it decided.
void render(const Config& cfg, int windowW, int windowH);

} // namespace spirit_hud
