#pragma once

#include "ecs/GameConfig.h"

#include <string>

namespace SaveManager
{

// Platform-standard save directory (e.g. %APPDATA%/PrisonEscapeGame/ on Windows).
// Cached after first call. Falls back to "saves/" if SDL_GetPrefPath fails.
std::string getSaveDir();

// Full path to save file: getSaveDir() + "save.json".
std::string defaultSavePath();

// Copy old saves/save.json to the new %APPDATA% location if it exists and the
// new location doesn't. Call once at startup before load().
void migrateOldSave();

// Load save data from disk. Returns default SaveData if file doesn't exist.
SaveData load(const std::string& path = "");

// Write save data to disk. Creates parent directory if needed.
bool save(const SaveData& data, const std::string& path = "");

// Add a character to the save data.
void addCharacter(SaveData& data, const std::string& name);

// Remove a character and all their runs from save data.
void deleteCharacter(SaveData& data, const std::string& name);

// Record a completed run. Inserts into sorted runs list.
void recordRun(SaveData& data, const Run& run);

// Return the top N runs sorted by score descending.
std::vector<Run> topRuns(const SaveData& data, int count = 10);

// Compute score from RunStats using ScoringConfig weights.
int computeScore(const RunStats& stats, const ScoringConfig& cfg, bool escaped);

// Migrate save data from older schema versions to current.
void migrate(SaveData& data);

} // namespace SaveManager
