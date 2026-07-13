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

    // Base value per stat, keyed by name -- holds BOTH tiers (faculties and
    // secondary). Absent = 0. This is progression state, not authored config.
    std::unordered_map<std::string, int> stat_levels;

    // buff id -> level owned (absent = level 0). Faculty levels add these on top
    // of their base; the sum of all buff levels is Spirit.
    std::unordered_map<std::string, int> buff_levels;
};

// Loads authored faculty + secondary stat names + buff defs from
// config/faculties.json.
void load(GrowthState& state, const std::string& path);

// Base value of any stat by name (faculty or secondary). Unknown name -> 0.
int statLevel(const GrowthState& state, const std::string& name);

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
