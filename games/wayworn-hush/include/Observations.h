#pragma once

#include <deque>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// The observation system -- the game's core interactive verb. Facing an
// observable and pressing observe surfaces a thought whose depth depends on what
// else you've observed (stateful, Dark Souls-style); connecting the right
// observations auto-forms a conclusion. New understanding earns Spirit EXP,
// reported to the caller (the growth economy owns the running total; see
// Growth.h). See docs/design/OBSERVATION-SYSTEM.md. A simplified cousin of
// selva's lang (tiered text) + insight (observation graph).
namespace observations
{

// One tier of an observable's thought. `requires_ids` lists observable ids that
// must already be observed for this tier to be available. resolve returns the
// deepest available tier. Tier 0 (first in the list) has empty requires.
struct Tier
{
    std::string text;
    std::vector<std::string> requires_ids;
    int spirit_exp = 0; // earned the first time this tier is reached
};

// An authored observable point in the world. Observed by facing it (within
// radius) and pressing observe.
struct Observable
{
    std::string id;
    float x = 0.0f;
    float y = 0.0f;
    float radius = 48.0f;
    std::vector<Tier> tiers; // tier 0 first; deeper tiers gated by requires
};

// A higher-order thought that auto-forms when all required observables have been
// observed. Weightier reward than a single observation.
struct Conclusion
{
    std::string id;
    std::vector<std::string> requires_ids;
    std::string text;
    int spirit_exp = 0;
};

// Runtime state (registry context; persists to save).
struct State
{
    std::vector<Observable> observables; // authored, loaded once
    std::vector<Conclusion> conclusions; // authored, loaded once

    // observable id -> deepest tier index reached (report earned Spirit EXP only
    // when this increases). Presence of a key = "this observable has been
    // observed".
    std::unordered_map<std::string, int> observed;
    std::unordered_set<std::string> formed; // conclusion ids already formed
    std::deque<std::string> pending;        // thought lines waiting to surface
};

// Loads authored observables + conclusions from config/observations.json.
void load(State& state, const std::string& path);

// Outcome of an observe attempt, so callers can drive feedback (sound).
enum class Outcome
{
    None,       // nothing observable was faced
    Reobserved, // observed again, no new understanding (no Spirit EXP)
    NewTier,    // reached a new (deeper) tier -> Spirit EXP
    Conclusion  // this observe also formed a conclusion
};

// Result of an observe attempt: the outcome (for feedback) plus the Spirit EXP
// newly earned this call (0 unless a deeper tier and/or a conclusion was
// reached). The caller adds `earned` to the growth economy -- observations do
// not hold a running total (single source of truth; see Growth.h).
struct ObserveResult
{
    Outcome outcome = Outcome::None;
    int earned = 0;
};

// Observe whatever observable is faced from (px,py) looking toward (dir_x,dir_y).
// Queues the resolved thought, and auto-forms any conclusions whose requirements
// are now met (queuing their text). Reports the outcome + Spirit EXP earned this
// call for a newly-reached tier and any conclusions formed.
ObserveResult observe(State& state, float px, float py, float dir_x, float dir_y);

// True if an observable is faced (for the can-observe affordance). Pure query.
bool facingObservable(const State& state, float px, float py, float dir_x, float dir_y);

// Id of the observable the player faces (nearest in-range within the facing
// cone), or "" if none. The single source of the facing geometry.
std::string facedId(const State& state, float px, float py, float dir_x, float dir_y);

// True if every tier currently available for this observable has been reached
// (nothing new to notice -> the glimmer should stay dim). Unknown id = false.
bool exhausted(const State& state, const std::string& observable_id);

} // namespace observations
