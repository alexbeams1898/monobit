#pragma once

#include <string>

// Player tuning + animation layout, loaded from config/player.json (config over
// constants: speed and animation "feel" values live in data, not C++ literals).
struct PlayerConfig
{
    float speed = 130.0f;        // world px/sec (normal walk)
    float run_speed_mult = 1.7f; // held-Shift fast-walk multiplier

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
};

// Loads config/player.json. Missing fields fall back to the struct defaults, so
// a partial or absent file still yields a usable config.
PlayerConfig loadPlayerConfig(const std::string& path);
