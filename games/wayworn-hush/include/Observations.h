#pragma once

#include "Growth.h"
#include "UnlockCondition.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// The cognition engine -- the game's core interactive verb. Two kinds of
// understanding (see docs/design/PROCESSING-MODEL.md):
//   OBSERVATION -- objective, DETERMINISTIC threshold tiers (no roll). What you
//      can factually perceive at your acuity. Earns Spirit EXP once per tier;
//      reaching a new tier can mint fresh thought-rolls.
//   THOUGHT -- subjective, a ROLL, recorded once. ONE struct whose character
//      is emergent in how many observations it needs: fed by ONE observation it
//      reads as a "thought" (an association); fed by a SERIES it reads as a
//      "conclusion" (a cross-event synthesis that yields unlocks). A thought's
//      inputs are always OBSERVATIONS (never other thoughts). A miss re-opens
//      only when its unlock_when advances further (anti-abuse).
// "Memory" is not a stat -- it is the accumulated record (observed / thoughts
// / flags) these draw on. Faculties: wonder / reason / perception. Spirit EXP is
// reported to the caller (growth owns the total; see Growth.h).
namespace observations
{

// Random nudge source for thought rolls: given n, returns an int in [0, n].
// Injected so the game passes a real RNG and tests force deterministic outcomes.
using RollRng = std::function<int(int)>;

// One objective reading tier. Deterministic: you get the deepest tier whose
// `unlock_when` is satisfied (usually stat thresholds). No roll, no rarity.
struct ObservationTier
{
    unlock::Condition unlock_when; // when this depth is perceivable (empty = base)
    std::string text;
    int spirit_exp = 0; // DERIVED at load from the observable's `value` (not authored)
};

// A deed offered on a spot after observing it (the action menu; see
// docs/design/ACTIONS.md). Grounded in real acts: it speaks (`result_text`),
// changes the world (`set_flag` -> the ambient engine re-checks, which is how a
// missed thought comes back), and is one-shot or repeatable. Sourced from a
// kind's defaults + per-spot overrides, resolved once at load.
struct Action
{
    std::string id;
    std::string label;             // the deed as the player chooses it
    unlock::Condition unlock_when; // when it is OFFERED (empty = always)
    std::string result_text;       // what doing it feels like (surfaced on take)
    std::string set_flag;          // world-state it sets (empty = none)
    bool one_shot = false;         // true = leaves the menu once taken
};

// A subjective thought -- ONE struct for both "thoughts" (fed by one
// observation) and "conclusions" (fed by a series). A roll. `unlock_when`
// declares which OBSERVATIONS it needs (observed: X [AND Y ...]) plus any stat
// gate; a thought requiring many observations naturally reads as a synthesis.
// Fires permanently on a hit; a miss re-opens whenever a relevant input changes.
//
// VALUE model (see PROCESSING-MODEL.md). Authored: id, unlock_when, faculty,
// feeders, text, miss_text, set_flag, and `emotional_weight`. DERIVED at load:
// opening, centrality, importance, value, difficulty, spirit_exp.
//   importance = wObj*centrality + wEmo*emotional_weight -- how much a thought
//                MATTERS: objectively (centrality = how much of the web hinges on
//                it, upstream + downstream) plus an authored emotional/story
//                weight a graph can't see (a deathbed beat that connects to
//                nothing yet lands).
//   opening    = observable value it UNIQUELY reveals (leave-one-out). Distinct
//                from centrality's downstream (which counts thoughts, not spots).
//   value      = importance_weight*importance + opening_weight*opening -> reward.
//   difficulty = how HARD to arrive at (structural breadth+feeders + a quiet
//                value nudge). Separate from value; sets the roll threshold.
struct Thought
{
    std::string id;
    unlock::Condition unlock_when; // inputs (observations) + any stat gate
    std::string faculty;           // perception (association) / reason (synthesis)
    std::string text;              // shown on a hit
    std::string miss_text;         // shown on a MISS -- the faint "something here you
                                   // can't place" pull (failure is content)
    // Secondary-stat feeders: name -> levels-per-+1 (e.g. survival:2 = +1 per 2
    // survival levels). Lived experience aiding a synthesis. Mostly for
    // conclusions; empty for a plain association.
    std::unordered_map<std::string, int> feeders;
    // Yield (loop-closing; mostly for conclusions): set a flag on a hit, which
    // the ambient engine cascades. Revealing content is done by gating that
    // content's own unlock_when / visible_when on the flag -- the uniform
    // primitive, not a bespoke pointer here.
    std::string set_flag; // set a flag (empty = none)
    // The ONE authored worth dial: emotional/story significance a graph can't
    // see (a beat that connects to nothing yet lands). Usually 0 -- most thoughts'
    // worth is purely objective (centrality + opening).
    int emotional_weight = 0;

    // --- DERIVED at load ---
    int opening = 0;    // observable value this thought uniquely reveals
    int centrality = 0; // base worth of the web upstream + downstream of it
    int importance = 0; // wObj*centrality + wEmo*emotional_weight
    int value = 0;      // importance_weight*importance + opening_weight*opening
    int difficulty = 1; // 1..max_band, structural (+ quiet value nudge); the roll bar
    int spirit_exp = 0; // reward, proportional to value
    // Thoughts float free -- they are NOT owned by an observable. They fire
    // ambiently whenever their unlock_when becomes true (DE-passive style),
    // wherever the player is. What lights a spot's signal is a query over
    // unlock_when, not ownership.
};

// How a trigger fires. OBSERVE: deliberate -- get within interact_reach and press the
// observe verb (discrete objects you choose to study). ENTER: ambient -- fires on its own
// when you reach the box (areas / moods that wash over you), no button. The mode is
// authored per placement in LDtk, not baked into the object type, so any shape can use
// either mode. See docs/design/OBSERVATION-SYSTEM.md.
enum class Trigger
{
    Observe,
    Enter
};

// Parse a trigger mode from its LDtk enum string ("Enter" -> Enter, everything else incl.
// "" / "Observe" -> Observe). The ONE place the string<->enum mapping lives, so a new mode
// can't be handled differently on the JSON-fallback path vs. the LDtk-placement path.
Trigger triggerFromString(const std::string& s);

// An authored observable in the world. `value` is the one authored worth knob -- "how
// much noticing this matters" -- feeding tier EXP + every thought VALUE downstream (see
// docs/design/PROCESSING-MODEL.md).
struct Observable
{
    std::string id;
    // Placement is an AABB authored in LDtk (the Observable box) and applied at load. The
    // box marks WHERE the thing is; you interact when within `interact_reach` of it (glow +
    // observe). (x, y) is the box CENTER, (w, h) its size, all world px. observations.json
    // holds the CONTENT (tiers/thoughts/actions), not the location.
    float x = 0.0f;
    float y = 0.0f;
    float w = 32.0f;
    float h = 32.0f;
    Trigger trigger = Trigger::Observe;
    bool fired = false;                 // ENTER: fired once already (edge-trigger)
    int value = 1;                      // authored worth (the shared currency)
    std::string kind;                   // action-kind name (defaults source; empty = none)
    unlock::Condition visible_when;     // hidden until satisfied (empty = always visible)
    std::vector<ObservationTier> tiers; // objective (base first, then deeper)
    std::vector<Action> actions;        // RESOLVED at load (kind defaults + overrides)

    // Distance from (px,py) to the nearest edge of the box (0 inside). The single spatial
    // measure: within `reach` of this drives both the glow and the observe interaction.
    float distanceTo(float px, float py) const
    {
        const float ddx = std::max({0.0f, x - w * 0.5f - px, px - (x + w * 0.5f)});
        const float ddy = std::max({0.0f, y - h * 0.5f - py, py - (y + h * 0.5f)});
        return std::sqrt(ddx * ddx + ddy * ddy);
    }
};

// Tuning for the thought roll + value model (config over constants).
//
// value      = importance_weight*importance + opening_weight*opening -> REWARD.
//   importance = centrality_weight*centrality + emotion_weight*emotional_weight.
//   centrality (derived) = base worth of the web up/downstream of the thought.
//   emotional_weight (authored) = story significance a graph can't see.
// difficulty = how HARD to arrive at -> the roll threshold. Mostly STRUCTURAL
//   (breadth + feeders), with value only a QUIET capped nudge.
struct RollConfig
{
    int dice = 4; // random nudge added to a roll: rand(0..dice)
    // Threshold placement: difficultyFrac * capacityMax * tightness. tightness < 1
    // so even a top-difficulty thought is landable before stats are capped.
    float tightness = 0.75f;
    int stat_cap = 20; // assumed reachable ceiling per stat (matches the F1 sliders)
    int max_band = 5;  // difficulty scale ceiling (Common..Legendary = 1..5)
    // importance = centrality_weight*centrality + emotion_weight*emotional_weight.
    // Centrality (objective, how much hinges on it) leads; emotion is the authored
    // nudge for beats a graph can't see. Both tunable.
    float centrality_weight = 1.0f;
    float emotion_weight = 1.0f;
    // value = importance_weight*importance + opening_weight*opening. Two roads to
    // worth (it MATTERS vs it OPENS UP), weighted ~equally to start.
    float importance_weight = 1.0f;
    float opening_weight = 1.0f;
    // Difficulty = clamp( structuralLoad + min(value_weight*value, value_nudge_cap),
    //                     1, max_band ). Structure (breadth of observations fused +
    // feeder count) is the LOUD term; the value nudge is quiet AND capped to
    // value_nudge_cap bands, so consequence can never leap the scale -- "obvious
    // major lore" (high value, simple structure) stays easy.
    float value_weight = 0.15f;
    int value_nudge_cap = 1; // most bands value alone can add to difficulty
    int exp_per_value = 5;   // Spirit EXP earned per point of value (reward scaling)
};

// Which kind a surfaced line is -- drives how the box styles it (plain objective
// observation vs colored subjective thought).
enum class LineKind
{
    Observation, // objective, plain
    Thought      // subjective, faculty + rarity styled
};

// A line waiting to surface. Carries what the box needs to style + animate it.
struct PendingLine
{
    LineKind kind = LineKind::Observation; // plain observation vs colored thought
    std::string text;
    std::string faculty; // thought only
    int difficulty = 0;  // thought only
    bool is_new = false; // first time reached -> the "New" badge
    // Spirit EXP this line is worth. The box surfaces the reward toast when it
    // actually DISPLAYS the line (not when the engine queued it), so "+N Spirit"
    // lands with the content on screen.
    int spirit_exp = 0;
};

// Runtime state = the pilgrim's accumulated record ("Memory is the game") plus
// authored content. No save format yet (registry context; the save slice will
// serialize this).
struct State
{
    std::vector<Observable> observables; // authored
    std::vector<Thought> thoughts;       // authored (thoughts + conclusions, one type)
    RollConfig roll;                     // authored tuning
    // Interaction reach (world px from an observable's box edge). Dead simple: get within
    // this of a box and it GLOWS + can be observed (Space); step away and it's dark. One
    // number drives both -- the object marks the spot, you walk up, it lights, you press.
    // (Souls-style: a trigger volume = box + reach.) One tuning knob for the whole game.
    float interact_reach = 40.0f;

    // Trigger index (built once at load): a "changed key" (see makeKey helpers) ->
    // indices into `thoughts` whose unlock_when references that key. A state
    // change only re-checks the thoughts that care -- O(affected), not O(all).
    std::unordered_map<std::string, std::vector<std::size_t>> trigger_index;

    // --- the record (what you've come to know) ---
    std::unordered_map<std::string, int> observed_tier; // spot -> deepest tier reached
    std::unordered_set<std::string> fired;              // thought ids that landed
    std::unordered_set<std::string> flags;              // quest/event flags set
    std::unordered_set<std::string> taken;              // one-shot action ids performed

    std::deque<PendingLine> pending; // lines waiting to surface
};

// Loads authored observables + thoughts from config/observations.json and
// builds the trigger index. `actions_path` (optional) loads action-kind defaults
// from config/actions.json and resolves each observable's action list
// (kind defaults + per-spot overrides); omit/empty for no actions.
void load(State& state, const std::string& path, const std::string& actions_path = {});

// Where an observation lives in the world -- authored in LDtk (an entity carrying the
// observable id), applied to the loaded content by applyPlacements. Separates the
// CONTENT (observations.json) from the PLACEMENT (the map): move the entity, the
// observation follows, with no coordinate to hand-sync.
struct Placement
{
    std::string id;
    float x = 0.0f; // box center
    float y = 0.0f;
    float w = 32.0f; // box size
    float h = 32.0f;
    Trigger trigger = Trigger::Observe;
};

// Bind each placement onto the matching loaded Observable (its x/y/w/h/trigger).
// Returns the ids that had NO loaded observable AND the observables that got NO
// placement (both are authoring gaps worth surfacing: content with nowhere to be, or a
// placement pointing at nothing). Idempotent.
struct PlacementReport
{
    std::vector<std::string> placements_without_observable; // placed id has no content
    std::vector<std::string> observables_without_placement; // content has no location
};
PlacementReport applyPlacements(State& state, const std::vector<Placement>& placements);

// A thought reads as a "conclusion" (synthesis) rather than a "thought" when
// it is gated on a SERIES of memories -- any clause requiring 2+ observed inputs.
// One derived source of truth, shared by the engine and the dev view.
bool isSynthesis(const Thought& r);

// Outcome of an observe attempt, for feedback (sound).
// What a call into the engine produced, for the caller's feedback. Only the
// distinctions callers/tests actually use: nothing / a reading surfaced / a
// thought fired. (Deeper "re-observed vs new-tier" splits were never consumed.)
enum class Outcome
{
    None,     // nothing faced / no-op
    Surfaced, // a reading surfaced (a tier line, or a deed result) -- no thought
    Thought,  // a thought landed (thought or conclusion)
};

// Result of a call into the engine: outcome (for feedback) + Spirit EXP earned
// this call (the caller adds `earned` to growth -- observations hold no total).
struct ObserveResult
{
    Outcome outcome = Outcome::None;
    int earned = 0;
};

// Observe the observable within interact_reach of (px,py) -- reveal the deepest objective
// tier your stats meet (deterministic; EXP once per tier), then run the ambient engine
// over the keys that changed (this spot observed + its tier), which rolls any newly-
// available thoughts. Queues the surfaced lines and reports the outcome + total EXP.
ObserveResult observe(State& state, const growth::GrowthState& growth, float px, float py,
                      const RollRng& rng);

// Ambient triggers, checked every frame from the player's position: an ENTER observable
// fires when the player is within interact_reach of its box. Each fires ONCE (edge-
// surfaces like a deliberate observe. OBSERVE-mode observables are ignored here (they
// need the verb). This is what makes areas/moods wash over you without a button press.
// Returns EXP earned.
ObserveResult triggerProximity(State& state, const growth::GrowthState& growth, float px, float py,
                               const RollRng& rng);

// External event sets a quest/event flag, then runs the ambient engine (a flag
// change can satisfy a thought's unlock_when, DE-passive style). Returns EXP
// earned from any thoughts that fired.
ObserveResult setFlag(State& state, const growth::GrowthState& growth, const std::string& flag,
                      const RollRng& rng);

// The player's faculties changed (leveled) -- re-run the ambient engine over the
// stat keys (a higher stat can satisfy a thought, and re-opens misses).
// Returns EXP earned from any thoughts that fired.
ObserveResult evaluateStats(State& state, const growth::GrowthState& growth, const RollRng& rng);

// The actions currently OFFERED at a spot: its resolved action list filtered to
// those whose unlock_when is satisfied and (for one_shot) not already taken. This
// is what the post-observe menu shows. Pointers are into state (valid until load).
std::vector<const Action*> availableActions(const State& state, const growth::GrowthState& growth,
                                            const std::string& spot);

// Perform an action at a spot: surface its result_text, record it (if one_shot),
// set its flag, and run the ambient engine over that flag -- which can unlock a
// deeper tier or fire a thought that was missed. Returns the outcome + EXP earned.
// No-op (Outcome::None) if the action isn't currently offered.
ObserveResult takeAction(State& state, const growth::GrowthState& growth, const std::string& spot,
                         const std::string& action_id, const RollRng& rng);

// Pure query for the notification layer: the set of engagements CURRENTLY
// reachable but not yet consumed, so the game can tell the player "there's
// something new to go back to" (a deeper reading, or a deed you can now do). Ids:
//   "<spot>@<tier>"      -- a deeper observation tier whose gate is now met but
//                           you've only observed a shallower tier (go read deeper)
//   "<spot>:<actionid>"  -- an action now offered you haven't taken
// The game diffs this against what it has already announced to fire each toast
// exactly once. Thoughts are NOT included (they are the unsignposted payoff).
std::unordered_set<std::string> availableUnlocks(const State& state,
                                                 const growth::GrowthState& growth);

// True if an observable is within interact_reach (affordance query). Pure.
bool facingObservable(const State& state, const growth::GrowthState& growth, float px, float py);

// Id of the nearest observable within interact_reach of (px,py), or "".
std::string facedId(const State& state, const growth::GrowthState& growth, float px, float py);

// The world-legibility signal for an observable (drives the glimmer). Deliberately
// minimal: the glimmer marks only "there is something HERE TO LOOK AT." Thoughts
// are the emergent, subjective layer -- they fire ambiently as you observe / act /
// grow, and are NOT signposted (a quest-marker for thoughts would turn the
// player's own path of understanding into a checklist to hunt; the point is that
// YOUR choices carve YOUR subjectivity). So the signal has two states only.
enum class Signal
{
    Unobserved, // never observed -> warm glow when faced (come look)
    Observed,   // already observed -> quiet (nothing to signpost)
};

// Derive an observable's signal: Unobserved until it has been observed to any
// tier, then Observed. Pure; reads only the record.
Signal signalFor(const State& state, const growth::GrowthState& growth, const std::string& spot);

// Glow strength for an observable at player (px,py): 1 within interact_reach of its box
// (the same test that gates interaction, so a lit glow means "observable now"), 0 beyond
// or if hidden. Binary, not a fade. 0 for an unknown id.
float glowStrength(const State& state, const growth::GrowthState& growth, const std::string& spot,
                   float px, float py);

// Live status of a thought, for the dev cognition view. A miss is not a
// distinct state -- a satisfied+unfired thought simply re-rolls whenever a
// relevant input next changes (the trigger index handles this), so "missed" folds
// into Available.
enum class ThoughtStatus
{
    Fired,      // already landed
    Available,  // unlock_when satisfied + unfired -- it will roll on the next relevant change
    OutOfReach, // unlock_when not yet satisfied (needs a memory/stat/flag)
};

// Classify a thought's current status (dev-facing X-ray; not player UI).
ThoughtStatus statusOf(const State& state, const growth::GrowthState& growth, const Thought& r);

// A dev-facing, multi-line explanation of a thought's status: WHY it is where
// it is and WHAT flips it. For OutOfReach, lists each condition with pass/fail;
// for Missed, the missed-level vs current (grow to retry); for Available, the roll
// it will make + whether it currently passes; for Fired, "done". (Shows the math
// -- dev X-ray only, never player UI; see PROCESSING-MODEL.md UI honesty.)
std::vector<std::string> explainStatus(const State& state, const growth::GrowthState& growth,
                                       const Thought& r);

// A dev-facing, multi-line breakdown of how a thought's `centrality` was derived:
// each upstream memory (with its base worth) and each downstream thought (with its
// opening), summed. Recomputed on demand -- the pieces aren't stored on Thought.
// (Dev X-ray only; see PROCESSING-MODEL.md.)
std::vector<std::string> explainCentrality(const State& state, const Thought& r);

} // namespace observations
