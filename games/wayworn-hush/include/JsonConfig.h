#pragma once

#include <nlohmann/json.hpp>

#include <optional>
#include <string>

// The one place the "load a JSON config, permissive on missing/unparseable" policy
// lives. Every config loader in the game opens a file, parses with exceptions off, and
// checks is_discarded -- this collapses that boilerplate into a single call so the policy
// (and any future change, e.g. "warn on a malformed config") is enforced in ONE spot.
namespace config
{

// Load + parse a JSON file. Returns nullopt if the file is missing OR unparseable (both
// are the permissive "use defaults" case). Parsing never throws (allow_exceptions=false).
// A present-but-wrong-TYPE field still throws on the caller's .get<>()/.value() -- guard
// those at the point of use; this only owns the file+parse step.
std::optional<nlohmann::json> load(const std::string& path);

} // namespace config
