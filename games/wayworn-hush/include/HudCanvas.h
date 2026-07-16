#pragma once

#include <string>

// HUD reference canvas -- makes all HUD geometry resolution-independent so the
// same authored layout is correct at any window size / fullscreen / aspect ratio.
//
// HUD elements are authored in FRACTIONS of a 16:9 reference canvas (0..1 in x
// and y). At render, the canvas maps to a 16:9 SAFE AREA centered in the window
// (letterboxed on non-16:9 displays), by a uniform scale -- so text keeps its
// proportions and nothing drifts to the far periphery on ultrawide. The world
// already renders 16:9 + letterboxed; the HUD shares that safe area.
//
// See docs/design/HUD.md (Dimensions).
namespace hud
{

// A rectangle in window pixels (what UI draws with).
struct Rect
{
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
};

// The 16:9 safe area centered in the given window, in window pixels. All the
// canvas mappings below place content within this.
Rect safeArea(int windowW, int windowH);

// Map a canvas fraction (fx,fy in 0..1 of the 16:9 canvas) to a window pixel
// position within the safe area.
float x(float fx, int windowW, int windowH); // fx in 0..1 -> window px
float y(float fy, int windowW, int windowH); // fy in 0..1 -> window px

// Map a canvas rect (fractions 0..1) to a window-pixel Rect within the safe area.
Rect rect(float fx, float fy, float fw, float fh, int windowW, int windowH);

// Uniform HUD scale (safe-area width / canvas width) -- multiply authored canvas
// pixel sizes (font sizes, paddings) by this to scale them with the window.
float scale(int windowW, int windowH);

// HUD visibility mode (Elden-Ring style). The player's choice lives in
// settings::Settings (the settings screen writes it, the save carries it); config/hud.json
// authors only the DEFAULT it starts at.
//   Auto -- a region draws only when it holds content (calm, anti-crowd default).
//   On   -- region chrome is always drawn (persistent frame); content fills it.
//   Off  -- HUD regions never draw.
enum class Visibility
{
    Auto,
    On,
    Off
};

// The named HUD regions (canvas fractions), loaded from config/hud.json. Each is
// a screen-fixed overlay band (see docs/design/HUD.md): THOUGHT (upper, subjective),
// OBSERVATION (lower, objective + action menu), NOTIFICATION (right, at lower-band
// top). pad_x / pad_y are inner padding as canvas fractions.
struct Regions
{
    // Canvas-fraction rects {x,y,w,h}. Sensible defaults if the config is absent.
    Rect thought{0.20f, 0.10f, 0.60f, 0.22f};
    Rect observation{0.20f, 0.68f, 0.60f, 0.24f};
    Rect notification{0.80f, 0.68f, 0.18f, 0.24f};
    float pad_x = 0.018f;
    float pad_y = 0.022f;
};

// Load region fractions from config/hud.json (silent no-op keeping defaults if missing).
// Call once at startup. Visibility is NOT here: it is a player preference, and a copy on
// the layout would be a second place for it to be true. loadVisibility reads the authored
// default separately, for whoever seeds the preferences.
void loadRegions(Regions& out, const std::string& path);

// The authored DEFAULT visibility from config/hud.json ("visibility"), or `fallback` if
// absent. Seeds settings::Settings at boot; a save's stored choice then overrides it.
Visibility loadVisibility(const std::string& path, Visibility fallback);

// Resolve a canvas-fraction rect (e.g. Regions::thought) to a window-pixel Rect.
Rect resolve(const Rect& canvasRect, int windowW, int windowH);

} // namespace hud
