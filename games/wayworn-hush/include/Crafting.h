#pragma once

#include "Inventory.h"
#include "Psyche.h" // psyche::RollRng (the shared int(int) roll source)

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Crafting: combine materials into a made thing. Little-Alchemy -- the INGREDIENTS are the only
// requirement: the right things in the pot make the thing, always, no unlock gate. Making a recipe
// you didn't have RECORDS it (State::known -- the discovery, which the game rewards + a recipe book
// reads later); recipes can also be handed over by events. On top of the inputs->output substrate
// sit a stat+lottery OUTCOME and inverse-mastery XP. Coefficients live in config; the math lives in
// code (formulas pattern). See docs/design/CRAFTING.md. Pure over its inputs -- unit-testable.
namespace crafting
{

// A required material: an item id + how many.
struct Ingredient
{
    std::string item; // -> inventory::ItemDef
    int qty = 1;
};

// A made thing's persistence: a consumable is used up (its quality scales); a permanent is a
// key/tool/structure (deterministic -- it opens or it doesn't).
enum class OutputKind
{
    Consumable,
    Permanent
};

// How a consumable's outcome quality scales: a doing-layer stat name + a base. Better stat +
// a good roll -> a stronger result. Ignored for Permanent outputs.
struct Scaling
{
    std::string stat; // the doing-layer stat this recipe scales on (empty = no scaling)
    float base = 1.0f;
};

// One stat a craft exercises, with its relative share of the craft's XP.
struct StatShare
{
    std::string stat;
    float weight = 1.0f;
};

// An authored recipe (one JSON per file, config/recipes/*.json), keyed by a stable id.
struct Recipe
{
    std::string id;
    std::vector<Ingredient> inputs; // set of required materials (types + quantities)
    std::string output_item;        // -> inventory::ItemDef
    int output_qty = 1;
    OutputKind kind = OutputKind::Consumable;
    Scaling scaling; // consumable outcome scaling (ignored for Permanent)
    // Which stats this craft exercises, and in what proportion -- a tool-like make weights
    // craftsmanship over survival, an organic one the reverse. Shares are relative, not
    // percentages: the craft's total XP (xp_base through the mastery curve) is split across
    // them. Empty = the whole reward goes to Config::default_xp_stat.
    std::vector<StatShare> xp_stats;
    int xp_base = 0;          // pre-inverse-mastery XP, split across xp_stats
    std::string reveals_flag; // set on first successful craft (opens new understanding)
};

// The loaded recipes, keyed by id.
struct Registry
{
    std::unordered_map<std::string, Recipe> recipes;

    const Recipe* find(const std::string& id) const
    {
        const auto it = recipes.find(id);
        return it != recipes.end() ? &it->second : nullptr;
    }
};

// Discovery state (serializes like psyche's `taken`/`fired`): recipe ids the player has
// realized by attempting them while their gate held.
struct State
{
    std::unordered_set<std::string> known;
};

// Outcome + XP tuning (coefficients over constants; config/crafting.json). The math lives in
// the functions below; these are the dials.
struct Config
{
    // Outcome quality = scaling.base + quality_per_stat*craftStat + a roll in [0, quality_luck].
    float quality_per_stat = 0.05f;
    float quality_luck = 0.3f;
    float quality_max = 2.0f;
    // XP = xp_base scaled by an inverse-mastery factor. The factor is high when the outcome
    // exceeded your level (quality high, craftStat low) and low when routine (craftStat high).
    //   factor = clamp( xp_reach_gain*quality - xp_mastery_falloff*craftStat + 1, xp_floor, hi )
    float xp_reach_gain = 0.6f;
    float xp_mastery_falloff = 0.1f;
    float xp_floor = 0.15f; // a mastered craft still pays a little
    float xp_ceiling = 2.0f;
    // The stat a craft feeds when its recipe names none. Making things is workmanship by
    // default; a recipe whose act is really something else overrides it with its own xp_stats.
    std::string default_xp_stat = "craftsmanship";
    // The reward for LEARNING a recipe (its first successful craft -- discovery, not repetition):
    // EXP into a reading faculty + a Spirit EXP grant. The game layer enacts these on
    // Outcome.first_time; the pure model just reports the discovery. The faculty reward is exp,
    // not levels, so discovery feeds the same use curve every other gain does -- one path a stat
    // can rise by, and a fixed grant keeps its meaning as levels get more expensive.
    std::string learn_faculty = "perception"; // which faculty a first craft deepens
    int learn_faculty_exp = 25;               // EXP into that faculty for the discovery
    int learn_spirit_exp = 10;                // Spirit EXP granted for the discovery
};

// Load the recipes from a directory of JSON files (one per recipe; id = the JSON's "id" or the
// filename stem). Silent no-op if the dir is missing. Call once at startup.
void load(Registry& out, const std::string& dir);

// Load the outcome/XP tuning from config/crafting.json (silent no-op -> defaults if missing).
void loadConfig(Config& cfg, const std::string& path);

// The result of matching a set of SELECTED inputs against the recipe table.
struct Match
{
    const Recipe* recipe = nullptr; // the exactly-matched recipe, or null if none
    // For the near-miss signal when there's no exact match: the closest recipe + how much of
    // its required item TYPES the attempt covers (0..1). closeness 0 / nearest null = nothing
    // like it.
    const Recipe* nearest = nullptr;
    float closeness = 0.0f;
};

// Match a set of selected item ids against the registry. An EXACT match = the selection's set
// of types equals a recipe's input types (quantities are checked at craft time, not here).
// If no exact match, reports the nearest recipe by type-overlap fraction (the closeness signal
// that guides experimentation). ALL recipes are considered -- the ingredients are the only
// requirement; there is no unlock gate.
Match match(const std::vector<std::string>& selectedTypes, const Registry& registry);

// The outcome quality for a consumable recipe: scaling.base + a formula of the crafting stat +
// a bounded roll, clamped to quality_max. Deterministic for a Permanent output (returns 1.0).
// `craftStat` is the player's level in the recipe's scaling stat. Pure over the roll.
float outcomeQuality(const Recipe& recipe, int craftStat, const psyche::RollRng& rng,
                     const Config& cfg);

// The total XP this craft earns, INVERSE to mastery: reaching above your level (good outcome,
// low craft stat) pays more; routine work (high craft stat) pays little. 0 if the recipe grants
// no XP. Split across the recipe's stats by splitXp.
int xpGained(const Recipe& recipe, int craftStat, float quality, const Config& cfg);

// Divide a craft's total XP across the stats it exercises, by their authored weights. A recipe
// naming no stats sends the whole reward to cfg.default_xp_stat. Largest-remainder, so the parts
// sum to exactly `total` and no named stat with a positive weight is rounded away to nothing.
std::vector<std::pair<std::string, int>> splitXp(const Recipe& recipe, int total,
                                                 const Config& cfg);

// The result of a craft attempt.
struct Outcome
{
    bool made = false;       // did the craft succeed (had the materials)?
    std::string output_item; // what was made (empty on failure)
    int output_qty = 0;
    float quality = 1.0f; // consumable outcome quality
    // The stats this craft exercised and the XP each earned (the recipe's shares applied to the
    // inverse-mastery total). Same shape observing reports, so both feed the one growth hook.
    std::vector<std::pair<std::string, int>> stat_gains;
    int xp = 0;                // total XP earned (inverse-mastery), before the share split
    std::string revealed_flag; // a flag to set (first-craft reveal), empty otherwise
    bool first_time = false;   // was this the recipe's first successful craft?
};

// Attempt to craft `recipe`: verify + consume its inputs from the satchel (via inventory ops),
// grant the output, compute the outcome quality + XP, and report first_time (+ reveal flag) by
// READING `known`. Returns made=false and changes nothing if the satchel lacks the inputs. Does
// NOT mark the recipe known -- the caller owns learning (one place), so a first craft and a
// teach-by-deed can't double-insert. The game enacts the returned flag/XP + the learn.
Outcome craft(const Recipe& recipe, inventory::Satchel& satchel, const inventory::Registry& items,
              int craftStat, const psyche::RollRng& rng, const Config& cfg,
              const State& state);

} // namespace crafting
