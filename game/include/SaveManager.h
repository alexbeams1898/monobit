#pragma once

#include "ecs/GameConfig.h"

#include <string>

namespace SaveManager
{

// Load save data from disk. Returns default SaveData if file doesn't exist.
SaveData load(const std::string& path = "saves/save.json");

// Write save data to disk. Creates saves/ directory if needed.
bool save(const SaveData& data, const std::string& path = "saves/save.json");

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
