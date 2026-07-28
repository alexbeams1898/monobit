#pragma once

#include <string>
#include <unordered_map>

#include <entt/entt.hpp>

class EntityManager;

// The authored characters (config/npcs/*.json): WHO someone is -- art, display
// name, footprint. WHERE they stand is map authoring (an Npc entity naming an id
// here), and WHAT they say is observation content (a speaker-flagged encounter) --
// talking IS observing, through the same engine. See docs/design/GAME-SYSTEMS.md.
namespace npc
{

struct Config
{
    std::string id;
    std::string name;    // display name -- shown when their lines surface
    std::string texture; // sprite sheet in the character layout (walk/idle/run rows)
    int frame_width = 64;
    int frame_height = 64;
    int direction_count = 4;
    int max_frames_per_state = 9;
    // The idle row + its frame count (1 = a standing pose per direction).
    int idle_row = 1;
    int idle_frames = 1;
    float idle_duration = 0.0f; // seconds/frame; 0 = static
    // The walk cycle (character-sheet layout defaults), used when a scene walks
    // them somewhere. `walk_speed` is world px/sec.
    int walk_row = 0;
    int walk_frames = 9;
    float walk_duration = 0.09f;
    float walk_speed = 90.0f;
    // Foot collider (world px): a small solid box at the sprite's base, so they
    // Y-sort by where they stand and the player cannot walk through them.
    float collider_w = 22.0f;
    float collider_h = 12.0f;
};

struct Registry
{
    std::unordered_map<std::string, Config> npcs;
};

// Load every config/npcs/*.json into the registry (file stem = fallback id).
// Missing directory = empty registry, silently (a game with no NPCs yet is fine).
void load(Registry& reg, const std::string& dir);

// Spawn `cfg` standing at (x,y) world px, facing the cardinal `facing` (empty =
// south): idle pose, Y-sorted by feet, solid. Returns the entity.
entt::entity spawn(EntityManager& em, const Config& cfg, float x, float y,
                   const std::string& facing);

} // namespace npc
