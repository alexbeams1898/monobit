#pragma once

#include <string>
#include <vector>

// What the art pipeline wrote about a sprite (tools/art.py).
//
// Read rather than hardcoded, because the alternative is a frame size written down in two
// places -- the .aseprite file and a constant in the game -- which drift apart the first time
// a drawing is resized, and do it silently.
namespace sprite_def
{

struct Anim
{
    std::string name;
    int from = 0; // inclusive frame indices, as tagged in Aseprite
    int to = 0;
};

struct Def
{
    std::string sheet; // path the game loads
    int frame_w = 0;
    int frame_h = 0;
    int frames = 0;
    std::vector<int> durations_ms; // per frame, as authored on the timeline
    std::vector<Anim> anims;
    // Where the character stands, in frame pixels. Absent (-1) means the art carries no
    // "feet" slice and the caller falls back to the frame's bottom-centre.
    int anchor_x = -1;
    int anchor_y = -1;
    bool ok = false;
};

// Load one, e.g. "assets/sprites/player.json". A missing or malformed file returns ok=false
// and logs -- a sprite that fails to load should be loud, not a mystery box on screen.
Def load(const std::string& path);

// The first frame of a named tag, or -1 when the art does not carry it. Art that changes state
// is asked for the STATE by name -- reordering frames or adding one later then cannot silently
// point the game at the wrong picture, which a hardcoded index would.
int frameOf(const Def& def, const std::string& tag);

} // namespace sprite_def
