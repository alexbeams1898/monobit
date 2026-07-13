#pragma once

#include "HudCanvas.h"
#include "UIRenderer.h"

#include <string>

using FontHandle = int;

// A general on-screen notification (toast) channel -- transient "by the way..."
// messages that surface automatically for anything worth a light nudge: Spirit
// EXP gained, a thought forming, "1 new observation available," "1 new action,"
// found items, etc. Distinct from the thought box (which is deliberate, you-read-
// it content); notifications are ambient, non-blocking, and fade on their own.
// Lifted from the studio's prison-escape notification pattern.
namespace notify
{

// Set the display font + the NOTIFICATION region (canvas-fraction rect toasts
// anchor to) once after fonts + HUD regions are loaded.
void init(FontHandle font, const hud::Rect& region);

// Push a message; it rises and fades over ~2s. Newest at the bottom.
void push(const std::string& text, const Color& color);

// Tick timers + render active toasts. Call each frame in the UI pass.
void render(float dt, int window_w, int window_h);

} // namespace notify
