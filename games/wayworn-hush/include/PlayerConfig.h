#pragma once

#include <string>

// Player tuning + animation layout, loaded from config/player.json (config over
// constants: speed and animation "feel" values live in data, not C++ literals).
struct PlayerConfig
{
    float speed = 130.0f;         // world px/sec (normal walk)
    float run_speed_mult = 1.30f; // held-Shift fast-walk multiplier

    struct AnimState
    {
        int row = 0;
        int frames = 1;
        float duration = 0.0f; // seconds/frame; 0 = static
    };

    std::string texture;
    int frame_width = 32;
    int frame_height = 64;
    int direction_count = 4;
    int max_frames_per_state = 4;
    AnimState idle;
    AnimState walk;
    AnimState fast_walk;

    // Foot collider (world px): a small box at the sprite's base, much narrower than the
    // art frame. Pairs with the sprite (frame_*), so it's config -- swap the protagonist
    // art and retune here, not in code.
    float collider_w = 22.0f;
    float collider_h = 12.0f;

    // Corner-nudge PROBE distance (world px): how far to the side the game looks for an
    // opening when you walk dead-on into a wall. Bigger = rounds a corner from further out
    // (more forgiving). Does NOT set how fast you slide -- that's corner_slide. 0 disables.
    float corner_nudge = 8.0f;

    // How fast the deflection glide runs, as a fraction of walk speed. 1.0 = you slide around
    // an obstacle at full pace; lower = the slide is gentler than a free walk (less "slidy",
    // more deliberate). A feel knob: tune in-game.
    float corner_slide = 0.6f;
};

// Loads config/player.json. Missing fields fall back to the struct defaults, so
// a partial or absent file still yields a usable config.
PlayerConfig loadPlayerConfig(const std::string& path);
