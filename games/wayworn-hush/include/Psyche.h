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
namespace psyche
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
    int spirit_exp = 0; // DERIVED at load from the encounter's `value` (not authored)
    int stat_exp = 0;   // DERIVED: faculty EXP for observing this tier
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
    // The words the PLAYER says aloud on taking it (quoted under his name, pushed
    // BEFORE result_text -- so on a speaker encounter the exchange reads your line,
    // then their reply). Empty = the deed is silent. `label` may paraphrase; this
    // carries the actual words. The third voice: perception is unquoted, speakers
    // are quoted as themselves, `say` is quoted as YOU.
    std::string say;
    std::string set_flag; // world-state it sets (empty = none)
    // Item effects, declared as OPAQUE ids (like set_flag): the observation system carries
    // them but never interprets them -- the GAME enacts the grant (it owns the satchel/loot).
    // grant_item -> deposit that item; grant_table -> roll that loot table. So a "Pick up" /
    // "Gather" deed on an encounter-and-takeable thing lives in the same deed list as its
    // readings-deeds, with no inventory dependency here.
    std::string grant_item;   // item id to deposit on take (empty = none)
    std::string grant_table;  // loot table id to roll on take (empty = none)
    std::string grant_recipe; // recipe id to TEACH on take (empty = none) -- a deed that hands you
                              // a recipe outright (e.g. reading a cleared rock teaches the draught)
    bool one_shot = false;    // true = leaves the menu once taken
    // true = taking this deed REMOVES the encounter from the world (you took THE thing --
    // a lone pebble). false (default) = the spot persists (you took FROM it -- a sprig off
    // the bush, still there to examine). The game does the despawn (it owns the world entity).
    bool consumes_spot = false;
    // Which stat taking this deed EXERCISES, and by how much (passive growth, §5). ANY stat --
    // a contemplative deed may name a mental faculty, a laboring one a physical stat; the verb
    // doesn't restrict it. Empty grows_stat = the deed grows nothing directly (it may still
    // grow a stat via a thought it triggers). Authored, not derived: a deed is a discrete
    // choice with no `value` to scale from.
    std::string grows_stat;
    int grows_exp = 0;
    // How long DOING this takes, in in-world minutes (0 = the default deed cost).
    // Authored per deed because only the author knows the work: a word costs
    // nothing, a kettle a few minutes, clearing a trail most of a morning.
    double minutes = 0.0;
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
    // WHO says it, if anyone. Empty = a true thought: inner, unquoted, written to
    // the notebook. "player" = a remark in the player's voice (quoted under his
    // chosen name); an npc id = a remark in THEIR voice. Remarks are authored in
    // the sibling `remarks` list (voice defaults to "player" there) but live in
    // this same struct -- one engine entity, three voices; thoughts are written,
    // remarks are said, and remarks never reach the notebook.
    std::string voice;
    std::string voice_name; // display name for an npc voice, resolved after load

    // THE remark test -- the one place the voiced/written distinction is asked, so
    // every consumer (line routing, notebook membership, note record) agrees.
    bool isRemark() const
    {
        return !voice.empty();
    }
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
    int spirit_exp = 0; // Spirit currency reward, proportional to value
    int stat_exp = 0;   // faculty EXP reward (own faculty), proportional to value
    // Thoughts float free -- they are NOT owned by an encounter. They fire
    // ambiently whenever their unlock_when becomes true (DE-passive style),
    // wherever the player is. What lights a spot's signal is a query over
    // unlock_when, not ownership.
};

// How a trigger fires. OBSERVE: deliberate -- get within interact_reach and press the
// observe verb (discrete objects you choose to study). ENTER: ambient -- fires on its own
// when you reach the box (areas / moods that wash over you), no button. The mode is
// authored per placement in LDtk, not baked into the object type, so any shape can use
// either mode. See docs/design/PSYCHE.md.
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
struct Encounter
{
    std::string id;
    // WHICH placed thing locates this observable -- set by applyPlacements, carried so the
    // game can record a spot being consumed against the map. Empty until placed.
    std::string placement_id;
    // Placement is an AABB authored in LDtk (the Encounter box) and applied at load. The
    // box marks WHERE the thing is; you interact when within `interact_reach` of it (glow +
    // observe). (x, y) is the box CENTER, (w, h) its size, all world px. psyche.json
    // holds the CONTENT (tiers/thoughts/actions), not the location.
    float x = 0.0f;
    float y = 0.0f;
    float w = 32.0f;
    float h = 32.0f;
    Trigger trigger = Trigger::Observe;
    bool fired = false; // ENTER: fired once already (edge-trigger)
    int value = 1;      // authored worth (the shared currency)
    // Speech is observation content: a spot whose `speaker` names a character
    // (config/npcs id) reads as that character talking -- its tier readings surface
    // in quotes under their name, while any thoughts they trigger stay the player's
    // own. `speaker_name` is the display name, resolved from the npc registry after
    // load (authored once there, never duplicated here).
    std::string speaker;
    std::string speaker_name;
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
    // Faculty EXP earned per point of value -- feeds the passive, use-based stat growth
    // (docs/design/PSYCHE.md §5). A thought grants THIS to its own faculty; a
    // reading grants it to the reading's faculty. Rarity/tier already fold into `value`, so a
    // rare thought grows its faculty more, automatically.
    int stat_exp_per_value = 4;
};

// Which kind a surfaced line is -- drives how the box styles it, and how the
// teaching layer names it. Observation vs Impression is WHO INITIATED (the
// consent gradient, docs/design/GAME-SYSTEMS.md section 8): an observation is
// chosen -- he walked up and looked; an impression is pressed on him -- the
// senses take it in whether he looks or not (an alarm, a crash downstairs, a
// scene playing at him). Same content engine, different arrival -- the same way
// a remark is a thought that left through a mouth.
enum class LineKind
{
    Observation, // objective, chosen (the observe verb)
    Impression,  // objective, unbidden (Enter triggers, scenes, miss pulls,
                 // a deed's felt result)
    Remark,      // speech, quoted -- his own or another's (say lines, replies,
                 // voiced remarks)
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
    // Who says this line (display name). Empty = the player's own reading/thought;
    // set = the box renders it as speech (quoted, under the name).
    std::string speaker;
};

// Runtime state = the pilgrim's accumulated record ("Memory is the game") plus
// authored content. No save format yet (registry context; the save slice will
// serialize this).
struct State
{
    std::vector<Encounter> encounters; // authored
    std::vector<Thought> thoughts;     // authored (thoughts + conclusions, one type)
    RollConfig roll;                   // authored tuning
    // Interaction reach (world px from an encounter's box edge). Dead simple: get within
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

    // The player's display name (the renamable pilgrim), for player-voiced lines
    // (`say` deeds, spoken thoughts). RUNTIME state: set by the game at world-enter
    // after the save is applied; never authored, never saved here.
    std::string player_name;

    // What he is carrying, as bare item ids -- a MIRROR the game refreshes from the
    // satchel (psyche owns no inventory; see the `carrying` / `without` clauses).
    // RUNTIME state, never authored, never saved here (the satchel is the record).
    std::unordered_set<std::string> carrying;

    // A THOUGHT IS THE NOTEBOOK: it is where he articulates what he never says
    // aloud, so without one nothing finishes becoming a thought -- it stays
    // unfired (recoverable the moment he carries one) and `notebook_want_text`
    // surfaces instead, once per stretch of not having one. Authored: the item id
    // (empty = the rule is off) and the words. Remarks are exempt -- speech needs
    // no page. The want is said on every blocked attempt, not once.
    std::string notebook_item = "notebook";
    std::string notebook_want_text;
    // Thoughts the engine turned away for want of a notebook. Their own inputs
    // (a spot observed) have already happened and won't happen again -- looking at
    // a spot you have already seen pushes no new key -- so without this the thought
    // would be lost, not delayed. Picking a notebook up offers exactly these back:
    // what waited, and nothing else. Runtime, per walk.
    std::unordered_set<std::string> unwritten;
};

// Does `cond` hold against the CURRENT knowledge (observed things, fired thoughts,
// flags, stat levels)? The one public gate for systems outside the observation
// engine (scene starts, future world state) -- same satisfaction rules as every
// unlock inside it. An empty condition holds.
bool conditionMet(const State& state, const growth::GrowthState& growth,
                  const unlock::Condition& cond);

// Loads authored encounters + thoughts from config/psyche.json and
// builds the trigger index. `actions_path` (optional) loads action-kind defaults
// from config/actions.json and resolves each encounter's action list
// (kind defaults + per-spot overrides); omit/empty for no actions.
void load(State& state, const std::string& path, const std::string& actions_path = {});

// Where an observation lives in the world -- authored in LDtk (an entity carrying the
// observable id), applied to the loaded content by applyPlacements. Separates the
// CONTENT (psyche.json) from the PLACEMENT (the map): move the entity, the
// observation follows, with no coordinate to hand-sync.
struct Placement
{
    std::string id;
    // WHICH placed thing this is -- stable across map edits, and the handle the game
    // records permanent world changes against (a spot consumed). Distinct from `id`:
    // that says WHAT observation is here. Carried through, never interpreted here.
    std::string placement_id;
    float x = 0.0f; // box center
    float y = 0.0f;
    float w = 32.0f; // box size
    float h = 32.0f;
    Trigger trigger = Trigger::Observe;
};

// Bind each placement onto the matching loaded Encounter (its x/y/w/h/trigger).
// Returns the ids that had NO loaded observable AND the encounters that got NO
// placement (both are authoring gaps worth surfacing: content with nowhere to be, or a
// placement pointing at nothing). Idempotent.
struct PlacementReport
{
    std::vector<std::string> placements_without_encounter; // placed id has no content
    std::vector<std::string> encounters_without_placement; // content has no location
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
// this call (the caller adds `earned` to growth -- psyche holds no total).
struct ObserveResult
{
    Outcome outcome = Outcome::None;
    int earned = 0;
    // Item effects a taken deed declared (ids only -- the game does the actual grant, since
    // observations is inventory-ignorant). grant_item ids in `granted`; grant_table ids in
    // `gathered`. Empty for a plain reading/deed.
    std::vector<std::string> granted;  // item ids to deposit
    std::vector<std::string> gathered; // loot table ids to roll
    std::vector<std::string> taught;   // recipe ids a deed handed over (the game marks them known)
    // Thought ids that LANDED this call, in the order they fired. Reported as opaque ids like
    // the grants above: the game notes when they happened (observations doesn't know what a
    // notebook is). Empty when nothing new landed.
    std::vector<std::string> landed;
    // Faculty EXP earned this call: (faculty name, exp) per thought/reading that landed. The
    // game applies these via growth::recordUse -- observations reports the reward, like
    // `earned`, but never mutates the self (it takes growth const). Passive stat growth (§5).
    std::vector<std::pair<std::string, int>> stat_gains;
    // Set to the encounter's id when a taken deed's consumes_spot fires -- the game removes
    // that spot's world entity (glimmer + interactable). Empty otherwise.
    std::string consumed_spot;
    // In-world minutes the taken deed declared (Action::minutes; 0 = it named
    // none, so the game bills its default). Reported, never applied -- psyche
    // knows what a deed costs, the game owns the clock.
    double minutes = 0.0;
};

// Observe the encounter within interact_reach of (px,py) -- reveal the deepest objective
// tier your stats meet (deterministic; EXP once per tier), then run the ambient engine
// over the keys that changed (this spot observed + its tier), which rolls any newly-
// available thoughts. Queues the surfaced lines and reports the outcome + total EXP.
// Build the unlock::Knowledge view over the current observation record + growth (observed
// memories + fired thoughts, flags, stat levels) -- the same snapshot the ambient engine uses.
// The caller owns the backing containers (the returned Knowledge holds pointers into them and
// into `state`). Exposed so OTHER gated systems (crafting) check a recipe's unlock_when against
// the same knowledge, not a divergent copy.
unlock::Knowledge buildKnowledge(const State& state, const growth::GrowthState& growth,
                                 std::unordered_set<std::string>& observedOut,
                                 std::unordered_map<std::string, int>& statsOut);

ObserveResult observe(State& state, const growth::GrowthState& growth, float px, float py,
                      const RollRng& rng);

// Observe a SPECIFIC observable by id (what the interaction system calls once it has
// resolved the active target). Same reveal + ambient-engine as observe(); no-op if the id
// is unknown or the encounter is currently hidden (visible_when unmet). `impression`
// marks the reading as pressed rather than chosen (a scene firing content at the
// player) -- see LineKind.
ObserveResult observeById(State& state, const growth::GrowthState& growth, const std::string& id,
                          const RollRng& rng, bool impression = false);

// Is the encounter with this id present in the world right now? False if unknown or its
// visible_when is unmet. THE public answer to "does this spot exist" -- a hidden encounter
// is neither observable nor actionable, so this one query gates its interactable + glow, and
// observe/act can never disagree about whether it's there.
bool visible(const State& state, const growth::GrowthState& growth, const std::string& id);

// Ambient triggers, checked every frame from the player's position: an ENTER observable
// fires when the player is within interact_reach of its box. Each fires ONCE (edge-
// surfaces like a deliberate observe. OBSERVE-mode encounters are ignored here (they
// need the verb). This is what makes areas/moods wash over you without a button press.
// Returns EXP earned.
ObserveResult triggerProximity(State& state, const growth::GrowthState& growth, float px, float py,
                               const RollRng& rng);

// Force an authored remark to be said NOW, bypassing its gate and roll -- the
// world can force what he hears (a scene's voice through a door), never what he
// concludes. Fires once (a fired remark is a held memory like any other) and
// cascades like an ambient fire. No-op if the id is unknown, not a remark, or
// already said.
ObserveResult forceRemark(State& state, const growth::GrowthState& growth, const std::string& id,
                          const RollRng& rng);

// Substitute the pilgrim's chosen name into every authored text field holding
// the {player} placeholder -- tiers, deed labels/says/results, thoughts,
// remarks. Called once at world-enter (content reloads fresh per walk, so a
// rename or a new pilgrim can never see another walk's binding).
void bindPlayerName(State& state, const std::string& name);

// What he carries changed: mirror `held` onto the state and re-run the ambient
// engine over the items that came or went -- picking up the notebook is exactly
// when a thought that ached for want of one gets its second look. No-op (and no
// engine run) when nothing actually changed, so this is safe to call every tick.
ObserveResult evaluateCarried(State& state, const growth::GrowthState& growth,
                              const std::unordered_set<std::string>& held, const RollRng& rng);

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

// The world-legibility signal for an encounter (drives the glimmer). Deliberately
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

// Derive an encounter's signal: Unobserved until it has been observed to any
// tier, then Observed. Pure; reads only the record.
Signal signalFor(const State& state, const growth::GrowthState& growth, const std::string& spot);

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

} // namespace psyche
