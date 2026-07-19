#pragma once

#include <string>
#include <unordered_map>
#include <vector>

// The growth economy -- the "self" plus the doing layer that feeds it. Two tiers
// of stats: the reading-FACULTIES (wonder / reason / perception -- how deeply you
// read the world) and the SECONDARY stats (survival / craftsmanship -- the doing
// layer that feeds the faculties). Every stat has a base value in stat_levels;
// faculties additionally gain buff levels on top. Spirit EXP is earned from
// understanding and (later) spent to raise buffs. Stat NAMES + tiers are authored
// in config; stat VALUES are runtime/save-game progression. Buff effects are
// content-driven, so the authored buff set is empty until that content exists.
// See docs/design/GAME-SYSTEMS.md.
namespace growth
{

// A plain RGB triple (0..1) -- a faculty's display hue.
struct Rgb
{
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
};

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
    std::vector<std::string> faculties; // reading-self stat names (tier 1)
    std::vector<std::string> secondary; // doing-layer stat names (tier 2)
    std::vector<BuffDef> buff_defs;     // authored, loaded once (empty for now)

    // Display hue per faculty (for the observation-rarity coloring). Absent =
    // a neutral default.
    std::unordered_map<std::string, Rgb> faculty_colors;

    int spirit_exp = 0; // earned-and-unspent currency (spent to raise buffs)

    // Faculty EXP needed for the first level; the log curve stretches from here. A stat's
    // use-derived level is floor(log2(use / exp_per_level + 1)) -- so `exp_per_level` exp buys
    // level 1, then each further level costs progressively more (diminishing returns). Config
    // (faculties.json); a feel knob for how fast faculties climb.
    float exp_per_level = 20.0f;

    // Base value per stat, keyed by name -- holds BOTH tiers (faculties and
    // secondary). Absent = 0. The authored STARTING point + any non-use source.
    std::unordered_map<std::string, int> stat_levels;

    // How much each stat has been EXERCISED (raw use), keyed by name. A stat rises by
    // DOING its verb -- exercising a faculty grows it (see docs/design/OBSERVATION-SYSTEM.md
    // §5). The displayed level is base + a diminishing curve over this (statLevel), so early
    // use raises fast and later use slowly. This is the saved progression; the level is
    // derived, never stored.
    std::unordered_map<std::string, int> stat_use;

    // buff id -> level owned (absent = level 0). Faculty levels add these on top
    // of their base; the sum of all buff levels is Spirit.
    std::unordered_map<std::string, int> buff_levels;
};

// Loads authored faculty + secondary stat names + buff defs from
// config/faculties.json.
void load(GrowthState& state, const std::string& path);

// Effective level of any stat by name: its base (stat_levels) plus the level its accumulated
// USE has earned, via the diminishing log curve (see exp_per_level). Unknown name -> 0.
int statLevel(const GrowthState& state, const std::string& name);

// Record exercising a stat: add `exp` to its use, so its level rises (see docs §5). Called
// when a thought/reading of that faculty lands. No-op for empty name or exp <= 0.
void recordUse(GrowthState& state, const std::string& name, int exp);

// A stat's progress toward its next level, for the Self-tab bar (Skyrim-style: a normalized
// fill through THIS level's span, no raw numbers on screen). `fill` is 0..1 within the current
// level; `into`/`span` are the raw exp for a hover/debug readout. Level 0 with no use -> fill 0.
struct StatProgress
{
    int level = 0;     // current level (== statLevel minus base; the USE-derived part)
    float fill = 0.0f; // 0..1 through the current level
    int into = 0;      // exp accumulated INTO the current level
    int span = 0;      // exp the current level spans (into..span fills the bar)
};
StatProgress statProgress(const GrowthState& state, const std::string& name);

// Depth of the self: the sum of all buff levels owned. Monotonic in practice
// (levels only rise), so systems can read it as the overall growth readout --
// the rare-reading tier gate and the true-ending gate.
int spirit(const GrowthState& state);

// A faculty's effective level: its base stat value plus the levels of the buffs
// belonging to it. Unknown faculty -> 0.
int facultyLevel(const GrowthState& state, const std::string& faculty);

// The display hue for a faculty (for coloring its readings). Unknown/absent
// faculty -> a neutral off-white default.
Rgb facultyColor(const GrowthState& state, const std::string& faculty);

} // namespace growth
