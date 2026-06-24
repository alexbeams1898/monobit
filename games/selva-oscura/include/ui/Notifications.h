#pragma once

#include <glm/vec4.hpp>

#include <string>

// Bottom-right floating toast for transient feedback (item pickups,
// insight unlocks, evolution events).
//
// Each push() drops one toast onto a fixed-capacity ring. The
// renderer fades + slides each entry over 2 seconds. Color and
// icon are surfaced as data so callsites can encode meaning
// (rarity tint, NEW! badge, etc.) without coupling the renderer
// to gameplay state.

namespace selva::ui
{

struct Notification
{
    std::string text;
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    // Path to an icon texture to render at the toast's left edge.
    // Empty = no icon (text-only). Reserved for the per-item icon
    // we'll wire when sprites land.
    std::string icon_path;
    float timer = 0.0f;    // counts DOWN from DURATION to 0
    float y_offset = 0.0f; // counts UP from 0 as the toast rises
};

// Push a transient toast onto the stack. Stack is bounded; oldest
// entries fall off when full. `color` carries the semantic palette
// (gold for first-time discoveries, neutral for repeats, etc.).
void pushNotification(std::string text, glm::vec4 color, std::string icon_path = std::string{});

// Advance every active notification (timer down, y_offset up).
// Called once per frame before render.
void tickNotifications(float dt);

// Render the live stack to the foreground draw list. Position is
// bottom-right; entries stack upward, newest at the bottom.
void renderNotifications();

// Wipe the stack -- called from hardResetWorldForCharacter so a
// character switch doesn't carry the prior character's pickup
// toasts into the new session.
void clearNotifications();

} // namespace selva::ui
