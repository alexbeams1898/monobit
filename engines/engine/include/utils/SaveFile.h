#pragma once

#include <nlohmann/json_fwd.hpp>

#include <optional>
#include <string>

// ---------------------------------------------------------------------------
// SaveFile -- the generic half of a save system: where a save lives on disk and
// how a JSON document gets read/written there. Engine-level so every game shares
// one implementation of the platform save-directory dance and the read/write
// plumbing (rather than each game reinventing it -- it has been copied per-game
// twice already).
//
// The game keeps what is game-specific: its own save SHAPE (the struct + its
// to/from-JSON), its own schema version, and its own migration body. This module
// never knows what is in the document.
//
// Versioning contract: read the document, then migrate it, then use it -- never
// migrate during the read (see the game's migrate()). Reads are tolerant by
// convention: an absent key takes its default, which makes additive schema
// change free.
// ---------------------------------------------------------------------------

namespace engine::save
{

// The platform-standard save directory for a game, with a trailing separator
// (e.g. "%APPDATA%/<org>/<app>/" on Windows -- the platform nests them, so `org`
// is the studio and `app` the game; passing the same string for both yields a
// doubled folder). Cached per (org, app). Falls back to "saves/" if the platform
// lookup fails, so a save is always writable somewhere.
std::string dir(const std::string& org, const std::string& app);

// The conventional save path: dir(org, app) + file_name.
std::string path(const std::string& org, const std::string& app,
                 const std::string& file_name = "save.json");

// Read a JSON document from `file_path`. Returns nullopt if the file is missing
// or unparsable -- both mean "no save", which callers treat as a fresh start
// rather than an error. Never throws.
std::optional<nlohmann::json> readJson(const std::string& file_path);

// Write `doc` to `file_path`, creating the parent directory if needed. Returns
// false (and logs) if the directory or file can't be written. ATOMIC: the
// document lands beside the target and is moved onto it, so an interrupted
// write can never leave a half-written save where progress used to be.
bool writeJson(const nlohmann::json& doc, const std::string& file_path);

} // namespace engine::save
