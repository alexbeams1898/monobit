#pragma once

#include "TileMap.h"

#include <random>

namespace SpawnUtils
{

// Shared RNG for spawn position selection and essence rolls.
std::mt19937& getRng();

// Find a random walkable spawn position scoped to the player's current room.
// Falls back to the full map with a distance ring if the room search fails.
// Returns false if no candidates exist at all.
bool findSpawnPosition(const TileMap& tm, float px, float py, float nearDist, float farDist,
                       float& out_x, float& out_y);

// Find a random walkable spawn position in a specific room (by index).
// If the player is in that room, enforces nearDist exclusion zone.
// Returns false if no walkable tiles exist in the room.
bool findSpawnInRoom(const TileMap& tm, int roomIdx, float px, float py, float nearDist,
                     float& out_x, float& out_y);

// Pick a room index for the next spawn, distributing evenly across all rooms.
// Uses an internal counter that increments each call.
// skip_room: room index to exclude (e.g. rest room). Pass -1 to skip none.
int nextSpawnRoom(int room_count, int skip_room = -1);

// Reset the room round-robin counter (call at wave start).
void resetSpawnRoomCounter();

} // namespace SpawnUtils
