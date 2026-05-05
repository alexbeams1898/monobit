#pragma once

#include "FontManager.h"
#include "UIRenderer.h"

#include <string>

// ---------------------------------------------------------------------------
// NotificationSystem -- floating on-screen text for pickups, XP, level-ups.
// Replaces console std::cout messages with visual feedback.
//
// Usage:
//   NotificationSystem::push("+1 Bone Shard", {1, 0.5, 0, 1});
//   NotificationSystem::render(windowW, windowH);   // call each frame
// ---------------------------------------------------------------------------

class NotificationSystem
{
  public:
    static void init(FontHandle body_font, FontHandle title_font);

    // Push a new notification. It floats up and fades over ~2 seconds.
    static void push(const std::string& text, const Color& color);

    // Tick timers and render active notifications.
    static void render(float dt, int window_w, int window_h);
};
