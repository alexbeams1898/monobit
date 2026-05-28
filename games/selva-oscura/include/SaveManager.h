#pragma once

#include "AppState.h"

#include <string>

// ---------------------------------------------------------------------------
// SaveManager - JSON-on-disk persistence for Selva's SaveData.
//
// Modeled on games/prison-escape-game/include/SaveManager.h. Same mechanism
// (SDL_GetPrefPath for platform-standard save directory, nlohmann::json for
// serialization, schema_version migration). The data model is Selva's; the
// I/O pattern is reused verbatim.
//
// Save location: %APPDATA%/SelvaOscura/save.json on Windows, equivalent
// platform paths elsewhere via SDL_GetPrefPath. Falls back to "saves/" if
// SDL_GetPrefPath fails.
// ---------------------------------------------------------------------------

namespace selva::SaveManager
{

// Platform-standard save directory, cached after first call. Falls back to
// "saves/" if SDL_GetPrefPath fails.
std::string getSaveDir();

// Full path to the default save file: getSaveDir() + "save.json".
std::string defaultSavePath();

// Load save data from disk. Returns a default-constructed SaveData if the
// file does not exist or fails to parse. Runs migrate() on the loaded data.
SaveData load(const std::string& path = "");

// Write save data to disk. Creates parent directories if needed. Returns
// true on success.
bool save(const SaveData& data, const std::string& path = "");

// Add a new character profile (just a name in v1 schema).
void addCharacter(SaveData& data, const std::string& name);

// Remove a character by name. No-op if the name is not found.
void deleteCharacter(SaveData& data, const std::string& name);

// Migrate older schema versions to current. Called automatically by load().
// In v1 this is a no-op (schema is brand new); the function exists for
// forward-compatibility when fields are added.
void migrate(SaveData& data);

} // namespace selva::SaveManager
