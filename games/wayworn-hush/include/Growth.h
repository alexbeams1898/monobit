#pragma once

#include <string>
#include <unordered_map>
#include <vector>

// The growth economy -- the "self". Spirit EXP is earned from understanding
// (rare observations, achievements, major events) and spent to raise buffs
// grouped under three reading-faculties (wonder / reason / perception). A
// faculty's level and the overall Spirit level are DERIVED from the buff levels
// (never stored separately -- one source of truth). Buff effects are
// content-driven and defined with the quests that make them wanted, so the
// authored buff set is empty until that content exists.
// See docs/design/GAME-SYSTEMS.md.
namespace growth
{

// An authored buff: a named, upgradeable deepening of one faculty. Effects are
// applied by the systems that read the buff level (content-driven, TBD), so this
// def carries only identity + which faculty it belongs to + its ceiling.
struct BuffDef
{
    std::string id;
    std::string faculty; // one of GrowthState::faculties
    int max_level = 1;
};

// Runtime state (registry context; persists to save once the save slice lands).
struct GrowthState
{
    std::vector<std::string> faculties; // authored order, loaded once
    std::vector<BuffDef> buff_defs;     // authored, loaded once (empty for now)

    int spirit_exp = 0; // earned-and-unspent currency (spent to raise buffs)

    // buff id -> level owned (absent = level 0). The build. Spirit and per-
    // faculty levels are summed from this, so it is the sole source of truth.
    std::unordered_map<std::string, int> buff_levels;
};

// Loads authored faculties + buff defs from config/faculties.json.
void load(GrowthState& state, const std::string& path);

// Depth of the self: the sum of all buff levels owned. Monotonic in practice
// (levels only rise), so systems can read it as the overall growth readout --
// the rare-reading tier gate and the true-ending gate.
int spirit(const GrowthState& state);

// Sum of the levels of the buffs belonging to one faculty (the per-faculty
// depth). Unknown faculty -> 0.
int facultyLevel(const GrowthState& state, const std::string& faculty);

} // namespace growth
