#pragma once

#include <nlohmann/json_fwd.hpp>

#include <string>

// A FLOOR TYPE (config/floors/*.json): the whole recipe for a KIND of space -- its shape, its
// look, the pools of room templates it draws on, which holes it can grow, and how far a run of
// wall holes may carry before it stops growing them.
//
// A type may name a `base` and override only the keys that differ, so retuning the shape two
// kinds of space share happens once. Reading one is here rather than in either of its readers
// because the generator and the descent both need the same answer: what a floor is made of must
// not be able to mean two things.
namespace floor_types
{

// The type at `path` with its base chain folded in, nearest override winning. An unreadable or
// malformed file logs and comes back empty, which leaves every reader on its own defaults.
//
// The merge is RFC 7386: a key present in the type REPLACES the base's value outright rather
// than merging into it, so a type naming its own hole mix gets exactly that mix -- which is what
// a different sort of place wants, and what merging arrays would quietly prevent.
nlohmann::json read(const std::string& path);

} // namespace floor_types
