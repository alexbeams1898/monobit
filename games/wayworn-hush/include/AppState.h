#pragma once

#include <string>

// ---------------------------------------------------------------------------
// AppState -- what the program is doing, as opposed to what the world is doing.
//
// A plain value, not a scene hierarchy: transitions are assignments, and the
// phase is the one thing that gates whether the world ticks at all. In-game
// overlays (the pause page) layer on top of Playing rather than being phases --
// pausing is something the world does, not something the app becomes.
//
// The world is not built until the player commits to entering it: greeting a
// player must not require a loaded region. See docs/design/SHELL.md.
// ---------------------------------------------------------------------------

namespace app
{

enum class Phase
{
    Greeting, // the title: continue the walk, begin again, or leave
    Playing   // in the world
};

struct State
{
    Phase phase = Phase::Greeting;
    // The world exists (region loaded, player spawned). False during Greeting
    // until a commit builds it; systems that touch the world check this rather
    // than assuming an entity is there.
    bool world_built = false;
    // Who is walking: the active pilgrim's stable id (savegame::Data::id), NOT
    // their name -- a name is only what's shown, and two pilgrims may share one.
    // Empty while greeting.
    std::string active_id;
    // How many pilgrims the roster holds. Read from disk at boot and kept in step
    // when the roster changes, so the greeting can draw itself without hitting the
    // disk every frame.
    int pilgrim_count = 0;
};

} // namespace app
