#include "Observations.h"

#include "JsonConfig.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace observations
{
namespace
{
// makeKnowledge is defined further down (needs the key helpers); forward-declare
// so the facing/visibility code up here can assemble a Knowledge view.
unlock::Knowledge makeKnowledge(const State& s, const growth::GrowthState& g,
                                std::unordered_set<std::string>& observedIds,
                                std::unordered_map<std::string, int>& statLevels);

// An observable is visible (faceable, glowable) when its visible_when holds
// (against the given knowledge). Empty visible_when = always visible. Hidden
// spots don't exist to the player until a thought's yield reveals them.
bool observableVisible(const Observable& o, const unlock::Knowledge& k)
{
    return o.visible_when.any.empty() || unlock::satisfied(o.visible_when, k);
}

// --- spatial resolution ----------------------------------------------------
// The observable the player can interact with: the NEAREST visible one whose box is
// within interact_reach of the player (Souls-style: walk up, it lights, press). Nullptr
// if none in reach.
const Observable* nearestInReach(const State& s, const growth::GrowthState& g, float px, float py)
{
    std::unordered_set<std::string> observedIds;
    std::unordered_map<std::string, int> statLevels;
    const unlock::Knowledge k = makeKnowledge(s, g, observedIds, statLevels);
    const Observable* best = nullptr;
    float bestDist = s.interact_reach;
    for (const auto& o : s.observables)
    {
        if (!observableVisible(o, k))
            continue;
        const float d = o.distanceTo(px, py);
        if (d <= bestDist)
        {
            best = &o;
            bestDist = d;
        }
    }
    return best;
}

// --- trigger keys ----------------------------------------------------------
// A "changed key" is a plain string namespacing what changed. A thought's
// unlock_when is walked once at load to register it under each key it references,
// so a state change only re-checks thoughts that care.
std::string keyObserved(const std::string& id)
{
    return "obs:" + id;
}
std::string keyFlag(const std::string& f)
{
    return "flag:" + f;
}
std::string keyStat(const std::string& s)
{
    return "stat:" + s;
}

// Every key a condition references (for indexing): the observed memories, flag,
// and stat names of each clause.
void collectKeys(const unlock::Condition& cond, std::vector<std::string>& out)
{
    for (const auto& c : cond.any)
    {
        for (const auto& id : c.observed)
            out.push_back(keyObserved(id));
        if (!c.flag.empty())
            out.push_back(keyFlag(c.flag));
        for (const auto& [name, level] : c.stat)
            out.push_back(keyStat(name));
    }
}

// --- knowledge view (bridges State/growth to the pure unlock primitive) ----
// The observation record doubles as the observed-memory set: a memory id is
// "held" when its observable has been observed to any tier. We expose that plus
// flags and stat levels.
unlock::Knowledge makeKnowledge(const State& s, const growth::GrowthState& g,
                                std::unordered_set<std::string>& observedIds,
                                std::unordered_map<std::string, int>& statLevels)
{
    observedIds.clear();
    for (const auto& [id, tier] : s.observed_tier)
        observedIds.insert(id);
    // Stat levels: faculties (base + buffs) and secondary stats, by name.
    statLevels.clear();
    for (const auto& f : g.faculties)
        statLevels[f] = growth::facultyLevel(g, f);
    for (const auto& sName : g.secondary)
        statLevels[sName] = growth::statLevel(g, sName);

    // Fired thoughts are memories too -- add them to the observed set so a
    // synthesis can reference an earlier thought by id.
    for (const auto& id : s.fired)
        observedIds.insert(id);

    unlock::Knowledge k;
    k.observed = &observedIds;
    k.flags = &s.flags;
    k.stats = &statLevels;
    return k;
}

// --- the roll (self-scaling: threshold measured against a thought's OWN
// reachable capacity, so it is always landable-but-earned) -------------------

int feederBonus(const Thought& r, const growth::GrowthState& g)
{
    int bonus = 0;
    for (const auto& [stat, per] : r.feeders)
        if (per > 0)
            bonus += growth::statLevel(g, stat) / per;
    return bonus;
}

// The best roll someone maxed on THIS thought's own inputs could produce:
// its faculty at cap + each feeder at cap (scaled by per) + the max nudge.
int capacityMax(const State& s, const Thought& r)
{
    int cap = r.faculty.empty() ? 0 : s.roll.stat_cap;
    for (const auto& [stat, per] : r.feeders)
        if (per > 0)
            cap += s.roll.stat_cap / per;
    return cap + s.roll.dice;
}

// Threshold this thought's roll must beat. Its difficulty places the bar as a
// fraction of its own capacity ceiling: difficulty 1 -> 0 (trivial), max_band ->
// near the ceiling (hard but reachable). tightness keeps even the top landable
// before stats are fully capped.
int threshold(const State& s, const Thought& r)
{
    const int top = s.roll.max_band;
    const int d = std::clamp(r.difficulty, 1, top);
    const float frac = top > 1 ? static_cast<float>(d - 1) / static_cast<float>(top - 1) : 0.0f;
    return static_cast<int>(frac * static_cast<float>(capacityMax(s, r)) * s.roll.tightness);
}

bool rollLands(const State& s, const growth::GrowthState& g, const Thought& r, const RollRng& rng)
{
    if (r.faculty.empty())
        return true; // unconditional thought (no faculty to weight it)
    const int roll = growth::facultyLevel(g, r.faculty) + feederBonus(r, g) + rng(s.roll.dice);
    return roll >= threshold(s, r);
}

} // namespace

// --- loading ---------------------------------------------------------------
namespace
{
unlock::Condition parseCondition(const nlohmann::json& j)
{
    unlock::Condition cond;
    if (!j.is_array())
        return cond;
    for (const auto& cj : j)
    {
        unlock::Clause c;
        c.flag = cj.value("flag", std::string{});
        // `observed` accepts a single string or an array (all required).
        if (const auto it = cj.find("observed"); it != cj.end())
        {
            if (it->is_string())
                c.observed.push_back(it->get<std::string>());
            else if (it->is_array())
                for (const auto& v : *it)
                    c.observed.push_back(v.get<std::string>());
        }
        if (const auto it = cj.find("stat"); it != cj.end() && it->is_object())
            for (const auto& [name, lvl] : it->items())
                c.stat[name] = lvl.get<int>();
        cond.any.push_back(std::move(c));
    }
    return cond;
}

std::unordered_map<std::string, int> parseFeeders(const nlohmann::json& j)
{
    std::unordered_map<std::string, int> f;
    if (const auto it = j.find("feeders"); it != j.end() && it->is_object())
        for (const auto& [name, per] : it->items())
            f[name] = per.get<int>();
    return f;
}

// --- deriving VALUE / difficulty / EXP from the forward value graph ---------
//
// A thought, when it fires, produces two output keys: obs:<its id> (it is
// now a held memory) and flag:<set_flag>. Those outputs can (a) enable OTHER
// thoughts whose unlock_when is thereby satisfied, and (b) make observables
// VISIBLE whose visible_when is thereby satisfied. A thought's VALUE is the
// total authored `value` of every observable it ends up making reachable this
// way (transitive; thoughts are conduits, full credit on shared nodes).
//
// We answer reachability with a monotone fixpoint over the set of "produced
// keys" seeded from one thought: keep adding the outputs of any thought
// now enabled, until nothing new appears. `satisfiedBy` asks whether a condition
// holds given ONLY a set of held keys (base observations are always held, since
// an observable with no visible_when is reachable from the start).

// Does a condition hold using only the produced key set? A clause holds if all
// its observed ids and its flag are present as keys (stat gates are ignored here
// -- VALUE measures what firing OPENS, and the roll/growth handles capacity).
bool satisfiedByKeys(const unlock::Condition& cond, const std::unordered_set<std::string>& keys)
{
    if (cond.any.empty())
        return true;
    for (const auto& c : cond.any)
    {
        bool ok = true;
        for (const auto& id : c.observed)
            ok = ok && keys.count(keyObserved(id));
        if (!c.flag.empty())
            ok = ok && keys.count(keyFlag(c.flag));
        if (ok)
            return true;
    }
    return false;
}

// Observable value reachable when the thoughts in `firing` are allowed to
// fire. Computed as a fixpoint over two forward edges, run to convergence:
//   - a firing thought whose unlock_when is met adds its outputs
//     (obs:<its id>, flag:<set_flag>);
//   - an observable whose visible_when is met becomes visible -> the player can
//     observe it, so its obs:<id> is a held memory later thoughts build on.
// VALUE = sum of every visible observable's value. `skip` (may be empty) is a
// thought id excluded from firing -- used for the leave-one-out marginal.
int reachableValue(const State& s, const std::string& skip)
{
    std::unordered_set<std::string> produced;
    bool grew = true;
    while (grew)
    {
        grew = false;
        for (const auto& r : s.thoughts)
        {
            if (r.id == skip)
                continue;
            const std::string outKey = keyObserved(r.id);
            if (produced.count(outKey) || !satisfiedByKeys(r.unlock_when, produced))
                continue;
            produced.insert(outKey); // fires -> its outputs
            if (!r.set_flag.empty())
                produced.insert(keyFlag(r.set_flag));
            grew = true;
        }
        for (const auto& o : s.observables)
        {
            const std::string obsKey = keyObserved(o.id);
            if (produced.count(obsKey) || !satisfiedByKeys(o.visible_when, produced))
                continue;
            produced.insert(obsKey); // now visible -> observable -> a held memory
            grew = true;
        }
    }
    int total = 0;
    for (const auto& o : s.observables)
        if (produced.count(keyObserved(o.id)))
            total += o.value;
    return total;
}

// How much a thought STRUCTURALLY demands to notice -- the loud term of
// difficulty, independent of value. Breadth = the most observations any one
// clause fuses (a wider synthesis is harder to assemble); feeders = how much
// lived experience it leans on. A one-observation link scores 1; a five-obs
// synthesis with two feeders scores 7.
int structuralLoad(const Thought& r)
{
    std::size_t breadth = 1;
    for (const auto& c : r.unlock_when.any)
        breadth = std::max(breadth, c.observed.size());
    return static_cast<int>(breadth) + static_cast<int>(r.feeders.size());
}

// Difficulty = structure (loud) + a QUIET, CAPPED value nudge. The value term is
// bounded to value_nudge_cap bands so consequence can only tip difficulty within
// a band or two -- never leap the scale. So "obvious major lore" (high value,
// breadth 1, no feeders) stays low-difficulty: value can't drag a structurally
// simple thought up to Legendary. Structure always dominates by construction.
int difficultyFor(const State& s, const Thought& r)
{
    const float nudge = std::min(s.roll.value_weight * static_cast<float>(r.value),
                                 static_cast<float>(s.roll.value_nudge_cap));
    const float raw = static_cast<float>(structuralLoad(r)) + nudge;
    return std::clamp(static_cast<int>(std::lround(raw)), 1, s.roll.max_band);
}

// The memory ids a thought directly requires (the observed inputs across all its
// unlock_when clauses) -- these are its direct predecessors (observations or
// earlier thoughts). Used to build the dependency graph for centrality.
std::vector<std::string> directRequires(const Thought& t)
{
    std::vector<std::string> out;
    for (const auto& c : t.unlock_when.any)
        for (const auto& id : c.observed)
            out.push_back(id);
    return out;
}

// Cycle-free BASE WORTH of a memory id, used to weight centrality: an
// observation's authored value, a thought's `opening` (must be computed first).
// Neither depends on centrality, so centrality is well-defined in one pass.
int baseWorth(const State& s, const std::string& id)
{
    for (const auto& o : s.observables)
        if (o.id == id)
            return o.value;
    for (const auto& t : s.thoughts)
        if (t.id == id)
            return t.opening;
    return 0;
}

// Transitive predecessor ids of a thought (everything upstream that had to be
// held for it to become possible), following `directRequires` through thoughts.
std::unordered_set<std::string> upstreamIds(const State& s, const Thought& start)
{
    std::unordered_set<std::string> seen;
    std::vector<std::string> stack = directRequires(start);
    while (!stack.empty())
    {
        const std::string id = stack.back();
        stack.pop_back();
        if (!seen.insert(id).second)
            continue;
        for (const auto& t : s.thoughts) // if id is a thought, walk ITS inputs too
            if (t.id == id)
                for (const auto& dep : directRequires(t))
                    stack.push_back(dep);
    }
    return seen;
}

// centrality(t) = base worth of the web upstream of t (what it took to reach)
// PLUS base worth of the THOUGHTS downstream of t (its role as a hub). Downstream
// counts thoughts only, so it never double-counts `opening` (which owns the
// downstream OBSERVABLE value). Weighted by cycle-free base worth.
void deriveCentrality(State& state)
{
    // Precompute each thought's upstream set once.
    std::unordered_map<std::string, std::unordered_set<std::string>> up;
    for (const auto& t : state.thoughts)
        up[t.id] = upstreamIds(state, t);

    for (auto& t : state.thoughts)
    {
        int upstream = 0;
        for (const auto& id : up[t.id])
            upstream += baseWorth(state, id);
        // Downstream thoughts = those whose upstream set contains t.
        int downstream = 0;
        for (const auto& other : state.thoughts)
            if (other.id != t.id && up[other.id].count(t.id))
                downstream += other.opening;
        t.centrality = upstream + downstream;
    }
}

// Stage the whole value model. Order matters: opening is cycle-free and feeds
// centrality's base worth; centrality + authored emotional_weight -> importance;
// importance + opening -> value; value -> difficulty nudge + reward.
void deriveValues(State& state)
{
    const int withAll = reachableValue(state, /*skip=*/std::string{});

    // 1) opening (leave-one-out observable value each thought uniquely reveals).
    for (auto& t : state.thoughts)
        t.opening = std::max(0, withAll - reachableValue(state, /*skip=*/t.id));

    // 2) centrality (needs opening as the downstream/base weight).
    deriveCentrality(state);

    // 3) importance -> value -> difficulty -> reward.
    for (auto& t : state.thoughts)
    {
        const float imp = state.roll.centrality_weight * static_cast<float>(t.centrality) +
                          state.roll.emotion_weight * static_cast<float>(t.emotional_weight);
        t.importance = static_cast<int>(std::lround(imp));
        const float val = state.roll.importance_weight * static_cast<float>(t.importance) +
                          state.roll.opening_weight * static_cast<float>(t.opening);
        t.value = static_cast<int>(std::lround(val));
        t.difficulty = difficultyFor(state, t); // structure + quiet value nudge
        t.spirit_exp = t.value * state.roll.exp_per_value;
    }

    // Observation tier EXP = the observable's value (scaled), earned once per tier.
    for (auto& o : state.observables)
        for (auto& t : o.tiers)
            t.spirit_exp = o.value * state.roll.exp_per_value;
}
} // namespace

namespace
{
void parseRollConfig(const nlohmann::json& j, RollConfig& roll)
{
    const auto it = j.find("roll");
    if (it == j.end())
        return;
    roll.dice = it->value("dice", roll.dice);
    roll.tightness = it->value("tightness", roll.tightness);
    roll.stat_cap = it->value("stat_cap", roll.stat_cap);
    roll.max_band = it->value("max_band", roll.max_band);
    roll.value_weight = it->value("value_weight", roll.value_weight);
    roll.value_nudge_cap = it->value("value_nudge_cap", roll.value_nudge_cap);
    roll.centrality_weight = it->value("centrality_weight", roll.centrality_weight);
    roll.emotion_weight = it->value("emotion_weight", roll.emotion_weight);
    roll.importance_weight = it->value("importance_weight", roll.importance_weight);
    roll.opening_weight = it->value("opening_weight", roll.opening_weight);
    roll.exp_per_value = it->value("exp_per_value", roll.exp_per_value);
}

Action parseAction(const nlohmann::json& a)
{
    Action act;
    act.id = a.value("id", std::string{});
    act.label = a.value("label", std::string{});
    act.result_text = a.value("result_text", std::string{});
    act.set_flag = a.value("set_flag", std::string{});
    act.grant_item = a.value("grant_item", std::string{});
    act.grant_table = a.value("grant_table", std::string{});
    act.one_shot = a.value("one_shot", false);
    act.consumes_spot = a.value("consumes_spot", false);
    if (const auto it = a.find("unlock_when"); it != a.end())
        act.unlock_when = parseCondition(*it);
    return act;
}

Observable parseObservable(const nlohmann::json& e)
{
    Observable o;
    o.id = e.value("id", std::string{});
    // Placement (the box: x/y/w/h + trigger) is authored in LDtk and applied later. Any
    // JSON values are only a fallback for content not yet placed in the map.
    o.x = e.value("x", 0.0f);
    o.y = e.value("y", 0.0f);
    o.w = e.value("w", 32.0f);
    o.h = e.value("h", 32.0f);
    o.trigger = triggerFromString(e.value("trigger", std::string{}));
    o.value = e.value("value", 1);
    o.kind = e.value("kind", std::string{});
    if (const auto it = e.find("visible_when"); it != e.end())
        o.visible_when = parseCondition(*it);
    for (const auto& t : e.value("tiers", nlohmann::json::array()))
    {
        ObservationTier tier;
        tier.text = t.value("text", std::string{});
        if (const auto it = t.find("unlock_when"); it != t.end())
            tier.unlock_when = parseCondition(*it);
        o.tiers.push_back(std::move(tier));
    }
    return o;
}

Thought parseThought(const nlohmann::json& e)
{
    Thought r;
    r.id = e.value("id", std::string{});
    r.faculty = e.value("faculty", std::string{});
    r.text = e.value("text", std::string{});
    r.miss_text = e.value("miss_text", std::string{});
    r.set_flag = e.value("set_flag", std::string{});
    r.emotional_weight = e.value("emotional_weight", 0);
    r.feeders = parseFeeders(e);
    if (const auto it = e.find("unlock_when"); it != e.end())
        r.unlock_when = parseCondition(*it);
    return r;
}

// kind name -> its default action list, parsed from config/actions.json.
using ActionKinds = std::unordered_map<std::string, std::vector<Action>>;

ActionKinds loadActionKinds(const std::string& actions_path)
{
    ActionKinds kinds;
    if (actions_path.empty())
        return kinds;
    const auto loaded = config::load(actions_path);
    if (!loaded)
        return kinds;
    const nlohmann::json& j = *loaded;
    const auto ak = j.find("action_kinds");
    if (ak == j.end() || !ak->is_object())
        return kinds;
    for (const auto& [name, kj] : ak->items())
    {
        std::vector<Action> acts;
        if (const auto it = kj.find("actions"); it != kj.end() && it->is_array())
            for (const auto& a : *it)
                acts.push_back(parseAction(a));
        kinds[name] = std::move(acts);
    }
    return kinds;
}

// Iterate an override sub-array (remove/replace/add) safely: only if present and
// an array. Returns an empty array otherwise. Avoids value()-on-null footguns.
const nlohmann::json& arrayOr(const nlohmann::json& obj, const char* key)
{
    static const nlohmann::json kEmpty = nlohmann::json::array();
    const auto it = obj.find(key);
    return (it != obj.end() && it->is_array()) ? *it : kEmpty;
}

// Resolve an observable's final action list: its kind's defaults, then per-spot
// remove / replace / add overrides (from the `actions` block in its json).
void resolveActions(Observable& o, const nlohmann::json& e, const ActionKinds& kinds)
{
    if (const auto it = kinds.find(o.kind); it != kinds.end())
        o.actions = it->second; // start from kind defaults
    else if (!o.kind.empty())
        // A non-empty kind that matches no action-kind = a typo or a missing actions.json
        // entry: the observable silently gets NO default actions. Log the gap.
        std::fprintf(stderr, "[observe] observable '%s' has kind '%s' with no action-kind\n",
                     o.id.c_str(), o.kind.c_str());

    const auto ov = e.find("actions");
    if (ov == e.end() || !ov->is_object())
        return;

    // remove: drop any default action by id.
    for (const auto& rid : arrayOr(*ov, "remove"))
    {
        const std::string id = rid.get<std::string>();
        o.actions.erase(std::remove_if(o.actions.begin(), o.actions.end(),
                                       [&](const Action& a) { return a.id == id; }),
                        o.actions.end());
    }
    // replace: swap a default's fields for an authored one (matched by id).
    for (const auto& r : arrayOr(*ov, "replace"))
    {
        const Action rep = parseAction(r);
        for (auto& a : o.actions)
            if (a.id == rep.id)
                a = rep;
    }
    // add: append bespoke actions for this spot.
    for (const auto& a : arrayOr(*ov, "add"))
        o.actions.push_back(parseAction(a));
}
} // namespace

Trigger triggerFromString(const std::string& s)
{
    // Values match the LDtk Trigger enum exactly (LDtk capitalizes enum ids).
    if (s == "Enter")
        return Trigger::Enter;
    return Trigger::Observe; // default + explicit "Observe"
}

void load(State& state, const std::string& path, const std::string& actions_path)
{
    state = State{};
    const auto loaded = config::load(path);
    if (!loaded)
        return;
    const nlohmann::json& j = *loaded;

    parseRollConfig(j, state.roll);
    state.interact_reach = j.value("interact_reach", state.interact_reach);

    const ActionKinds kinds = loadActionKinds(actions_path);
    for (const auto& e : j.value("observables", nlohmann::json::array()))
    {
        Observable o = parseObservable(e);
        resolveActions(o, e, kinds); // kind defaults + per-spot overrides
        if (!o.id.empty() && !o.tiers.empty())
            state.observables.push_back(std::move(o));
    }

    for (const auto& e : j.value("thoughts", nlohmann::json::array()))
    {
        Thought r = parseThought(e);
        if (!r.id.empty())
            state.thoughts.push_back(std::move(r));
    }

    // Build the trigger index: each thought registers under every key that can
    // change its outcome -- its unlock_when inputs, its roll FACULTY, and its
    // feeder stats -- so a state change only re-checks those that care, and any
    // relevant rise (faculty or feeder) re-opens a prior miss automatically.
    std::vector<std::string> keys;
    for (std::size_t i = 0; i < state.thoughts.size(); ++i)
    {
        const Thought& r = state.thoughts[i];
        keys.clear();
        collectKeys(r.unlock_when, keys);
        if (!r.faculty.empty())
            keys.push_back(keyStat(r.faculty));
        for (const auto& [feeder, per] : r.feeders)
            keys.push_back(keyStat(feeder));
        for (const auto& k : keys)
            state.trigger_index[k].push_back(i);
    }

    deriveValues(state);
}

PlacementReport applyPlacements(State& state, const std::vector<Placement>& placements)
{
    PlacementReport report;
    std::unordered_set<std::string> placed;
    for (const auto& p : placements)
    {
        placed.insert(p.id);
        bool matched = false;
        for (auto& o : state.observables)
            if (o.id == p.id)
            {
                o.x = p.x;
                o.y = p.y;
                o.w = p.w;
                o.h = p.h;
                o.trigger = p.trigger;
                matched = true;
            }
        if (!matched)
            report.placements_without_observable.push_back(p.id);
    }
    for (const auto& o : state.observables)
        if (!placed.count(o.id))
            report.observables_without_placement.push_back(o.id);
    return report;
}

bool isSynthesis(const Thought& r)
{
    for (const auto& c : r.unlock_when.any)
        if (c.observed.size() >= 2)
            return true;
    return false;
}

// --- the ambient engine ----------------------------------------------------
namespace
{
// Roll every unfired thought triggered by `changedKeys`; queue hits, apply
// yields (which enqueue more changed keys -> cascade). Returns EXP earned and
// whether anything fired. Fire-once bounds the cascade.
int runEngine(State& state, const growth::GrowthState& growth, std::vector<std::string> changedKeys,
              const RollRng& rng, bool& anyFired)
{
    int earned = 0;
    std::unordered_set<std::string> observedIds;
    std::unordered_map<std::string, int> statLevels;
    // The knowledge snapshot is stable until a thought fires (which mutates
    // fired/flags). Build it once and rebuild only after a fire -- not per
    // candidate.
    unlock::Knowledge k = makeKnowledge(state, growth, observedIds, statLevels);

    while (!changedKeys.empty())
    {
        const std::string key = changedKeys.back();
        changedKeys.pop_back();
        const auto idx = state.trigger_index.find(key);
        if (idx == state.trigger_index.end())
            continue;

        for (const std::size_t ri : idx->second)
        {
            const Thought& r = state.thoughts[ri];
            // Attemptable iff not already fired. Anti-abuse is structural: the
            // index only re-visits a thought when a key it references changes,
            // so a miss simply does nothing here and is re-checked automatically
            // when a real input (memory / stat / flag / feeder) next changes.
            if (state.fired.count(r.id))
                continue;
            if (!unlock::satisfied(r.unlock_when, k))
                continue; // not yet eligible -- nothing to surface
            if (!rollLands(state, growth, r, rng))
            {
                // Eligible but the roll missed: surface the faint "something here
                // you can't place" pull (failure is content). The trigger index
                // only re-visits this thought when a real input changes, so it
                // re-attempts (and can land) as the player grows -- no spam.
                if (!r.miss_text.empty())
                    state.pending.push_back(
                        PendingLine{LineKind::Observation, r.miss_text, {}, 0, false, 0});
                continue;
            }

            state.fired.insert(r.id);
            state.pending.push_back(PendingLine{LineKind::Thought, r.text, r.faculty, r.difficulty,
                                                /*is_new=*/true, r.spirit_exp});
            earned += r.spirit_exp;
            anyFired = true;

            // Yields cascade: a fired thought is itself a held memory, and may
            // set a flag -> new changed keys re-checked this same run. State
            // changed, so refresh the knowledge snapshot.
            changedKeys.push_back(keyObserved(r.id));
            if (!r.set_flag.empty() && state.flags.insert(r.set_flag).second)
                changedKeys.push_back(keyFlag(r.set_flag));
            k = makeKnowledge(state, growth, observedIds, statLevels);
        }
    }
    return earned;
}
} // namespace

namespace
{
// Reveal a specific observable's deepest satisfied tier and run the ambient engine over
// what changed -- the shared core of both the deliberate observe verb and the ambient
// enter/approach triggers. `o` is the resolved observable (already located + eligible).
ObserveResult fireObservable(State& state, const growth::GrowthState& growth, const Observable& o,
                             const RollRng& rng)
{
    std::unordered_set<std::string> observedIds;
    std::unordered_map<std::string, int> statLevels;
    const int prevTier = state.observed_tier.count(o.id) ? state.observed_tier.at(o.id) : 0;
    int best = 0;
    const ObservationTier* bestTier = nullptr;
    const unlock::Knowledge k = makeKnowledge(state, growth, observedIds, statLevels);
    for (std::size_t i = 0; i < o.tiers.size(); ++i)
        if (unlock::satisfied(o.tiers[i].unlock_when, k))
        {
            best = static_cast<int>(i) + 1;
            bestTier = &o.tiers[i];
        }
    if (best == 0)
        return {Outcome::None, 0};

    int earned = 0;
    Outcome outcome = Outcome::Surfaced; // a reading surfaced; upgraded if a thought fires

    // Surface the objective reading; EXP is earned (and carried on the line, so its toast
    // lands on display) only when a newly-reached tier.
    const bool newTier = best > prevTier;
    state.pending.push_back(PendingLine{LineKind::Observation,
                                        bestTier->text,
                                        {},
                                        0,
                                        /*is_new=*/newTier,
                                        newTier ? bestTier->spirit_exp : 0});
    std::vector<std::string> changedKeys;
    if (newTier)
    {
        state.observed_tier[o.id] = best;
        earned += bestTier->spirit_exp;
        changedKeys.push_back(keyObserved(o.id)); // now a held memory
    }

    bool anyFired = false;
    earned += runEngine(state, growth, std::move(changedKeys), rng, anyFired);
    if (anyFired)
        outcome = Outcome::Thought;
    return {outcome, earned};
}
} // namespace

ObserveResult observe(State& state, const growth::GrowthState& growth, float px, float py,
                      const RollRng& rng)
{
    const Observable* o = nearestInReach(state, growth, px, py);
    if (!o)
        return {Outcome::None, 0};
    return fireObservable(state, growth, *o, rng);
}

ObserveResult observeById(State& state, const growth::GrowthState& growth, const std::string& id,
                          const RollRng& rng)
{
    std::unordered_set<std::string> observedIds;
    std::unordered_map<std::string, int> statLevels;
    const unlock::Knowledge k = makeKnowledge(state, growth, observedIds, statLevels);
    for (const auto& o : state.observables)
        if (o.id == id)
            return observableVisible(o, k) ? fireObservable(state, growth, o, rng)
                                           : ObserveResult{Outcome::None, 0};
    return {Outcome::None, 0};
}

ObserveResult triggerProximity(State& state, const growth::GrowthState& growth, float px, float py,
                               const RollRng& rng)
{
    // Ambient triggers: an ENTER observable fires when the player reaches its box.
    // Fires ONCE (edge-triggered on `fired`), then reveals + runs the engine like a
    // deliberate observe. Visibility gating and the per-tier EXP-once rules are shared via
    // fireObservable. Called every frame.
    int earned = 0;
    Outcome outcome = Outcome::None;
    std::unordered_set<std::string> observedIds;
    std::unordered_map<std::string, int> statLevels;
    const unlock::Knowledge k = makeKnowledge(state, growth, observedIds, statLevels);
    for (auto& o : state.observables)
    {
        if (o.trigger != Trigger::Enter || o.fired)
            continue;
        if (!unlock::satisfied(o.visible_when, k))
            continue; // not yet present in the world
        if (o.distanceTo(px, py) > state.interact_reach)
            continue; // not within reach yet
        o.fired = true;
        const ObserveResult res = fireObservable(state, growth, o, rng);
        earned += res.earned;
        if (res.outcome != Outcome::None)
            outcome = res.outcome;
    }
    return {outcome, earned};
}

ObserveResult setFlag(State& state, const growth::GrowthState& growth, const std::string& flag,
                      const RollRng& rng)
{
    if (!state.flags.insert(flag).second)
        return {Outcome::None, 0}; // already set
    bool anyFired = false;
    const int earned = runEngine(state, growth, {keyFlag(flag)}, rng, anyFired);
    return {anyFired ? Outcome::Thought : Outcome::None, earned};
}

ObserveResult evaluateStats(State& state, const growth::GrowthState& growth, const RollRng& rng)
{
    // A stat changed: re-check every stat-keyed thought (gate stats AND feeder
    // stats are both indexed). Called only on an actual stat change, so the scan
    // over stat keys is rare, not per-frame.
    std::vector<std::string> keys;
    for (const auto& [key, ignored] : state.trigger_index)
        if (key.rfind("stat:", 0) == 0)
            keys.push_back(key);
    bool anyFired = false;
    const int earned = runEngine(state, growth, std::move(keys), rng, anyFired);
    return {anyFired ? Outcome::Thought : Outcome::None, earned};
}

namespace
{
const Observable* observableById(const State& s, const std::string& id)
{
    for (const auto& o : s.observables)
        if (o.id == id)
            return &o;
    return nullptr;
}

// Is this action currently offered? Its unlock_when must hold (against the given
// The `taken` key for a one-shot deed -- scoped by SPOT so a deed id shared across spots
// (kind defaults like "search") is tracked per-spot, not globally. The one place the format
// lives (availableUnlocks builds the same "spot:id" shape for its pull-back notify).
std::string takenKey(const std::string& spot, const std::string& actionId)
{
    return spot + ":" + actionId;
}

// knowledge snapshot), and a one_shot already taken AT THIS SPOT is no longer offered.
bool actionOffered(const State& s, const std::string& spot, const Action& a,
                   const unlock::Knowledge& k)
{
    if (a.one_shot && s.taken.count(takenKey(spot, a.id)))
        return false;
    return unlock::satisfied(a.unlock_when, k);
}
} // namespace

std::vector<const Action*> availableActions(const State& state, const growth::GrowthState& growth,
                                            const std::string& spot)
{
    std::vector<const Action*> out;
    const Observable* o = observableById(state, spot);
    if (!o)
        return out;
    std::unordered_set<std::string> observedIds;
    std::unordered_map<std::string, int> statLevels;
    const unlock::Knowledge k = makeKnowledge(state, growth, observedIds, statLevels);
    for (const auto& a : o->actions)
        if (actionOffered(state, spot, a, k))
            out.push_back(&a);
    return out;
}

ObserveResult takeAction(State& state, const growth::GrowthState& growth, const std::string& spot,
                         const std::string& action_id, const RollRng& rng)
{
    const Observable* o = observableById(state, spot);
    if (!o)
        return {Outcome::None, 0};
    std::unordered_set<std::string> observedIds;
    std::unordered_map<std::string, int> statLevels;
    const unlock::Knowledge k = makeKnowledge(state, growth, observedIds, statLevels);
    const Action* act = nullptr;
    for (const auto& a : o->actions)
        if (a.id == action_id && actionOffered(state, spot, a, k))
        {
            act = &a;
            break;
        }
    if (!act)
        return {Outcome::None, 0}; // not offered -> no-op

    // The deed's own voice (plain line), then its world-state change.
    if (!act->result_text.empty())
        state.pending.push_back(
            PendingLine{LineKind::Observation, act->result_text, {}, 0, /*is_new=*/false, false});
    if (act->one_shot)
        state.taken.insert(takenKey(spot, act->id));

    std::vector<std::string> changedKeys;
    if (!act->set_flag.empty() && state.flags.insert(act->set_flag).second)
        changedKeys.push_back(keyFlag(act->set_flag));

    // Item effects are passed up as ids (the game does the grant -- observations is
    // inventory-ignorant). Captured before runEngine so the return carries them.
    ObserveResult result;
    if (!act->grant_item.empty())
        result.granted.push_back(act->grant_item);
    if (!act->grant_table.empty())
        result.gathered.push_back(act->grant_table);
    if (act->consumes_spot)
        result.consumed_spot = spot; // the game despawns this observable's world entity

    // Run the ambient engine over the new flag -- this is what recovers a missed
    // thought or opens a deeper tier gated on the deed.
    bool anyFired = false;
    result.earned = runEngine(state, growth, std::move(changedKeys), rng, anyFired);
    result.outcome = anyFired ? Outcome::Thought : Outcome::Surfaced;
    return result;
}

std::unordered_set<std::string> availableUnlocks(const State& state,
                                                 const growth::GrowthState& growth)
{
    std::unordered_set<std::string> out;
    std::unordered_set<std::string> observedIds;
    std::unordered_map<std::string, int> statLevels;
    const unlock::Knowledge k = makeKnowledge(state, growth, observedIds, statLevels);

    for (const auto& o : state.observables)
    {
        if (!observableVisible(o, k))
            continue; // hidden spots aren't "available to go back to" yet

        // Only a spot you've already observed can pull you BACK -- an unseen spot
        // is discovered by its glimmer, not announced as an "unlock."
        const auto it = state.observed_tier.find(o.id);
        const int reached = it != state.observed_tier.end() ? it->second : 0;
        if (reached == 0)
            continue;

        // A deeper observation tier whose gate is now met but not yet observed.
        for (int i = reached; i < static_cast<int>(o.tiers.size()); ++i)
            if (unlock::satisfied(o.tiers[static_cast<std::size_t>(i)].unlock_when, k))
                out.insert(o.id + "@" + std::to_string(i + 1));

        // An action now offered that hasn't been taken.
        for (const auto& a : o.actions)
            if (actionOffered(state, o.id, a, k))
                out.insert(takenKey(o.id, a.id));
    }
    return out;
}

ThoughtStatus statusOf(const State& state, const growth::GrowthState& growth, const Thought& r)
{
    if (state.fired.count(r.id))
        return ThoughtStatus::Fired;
    std::unordered_set<std::string> observedIds;
    std::unordered_map<std::string, int> statLevels;
    const unlock::Knowledge k = makeKnowledge(state, growth, observedIds, statLevels);
    if (!unlock::satisfied(r.unlock_when, k))
        return ThoughtStatus::OutOfReach;
    return ThoughtStatus::Available; // eligible + unfired -- it will roll
}

namespace
{
// One condition's pass/fail annotation for the dev explanation.
void explainClause(const unlock::Clause& c, const unlock::Knowledge& k,
                   std::vector<std::string>& out)
{
    for (const auto& id : c.observed)
        out.push_back(std::string("  observed ") + id +
                      (k.has(k.observed, id) ? " OK" : " MISSING"));
    for (const auto& [name, lvl] : c.stat)
        out.push_back("  " + name + " >= " + std::to_string(lvl) + " (have " +
                      std::to_string(k.stat(name)) + ")" + (k.stat(name) >= lvl ? " OK" : " NO"));
    if (!c.flag.empty())
        out.push_back(std::string("  flag ") + c.flag +
                      (k.has(k.flags, c.flag) ? " OK" : " MISSING"));
}
} // namespace

std::vector<std::string> explainStatus(const State& state, const growth::GrowthState& growth,
                                       const Thought& r)
{
    std::vector<std::string> out;
    if (state.fired.count(r.id))
    {
        out.emplace_back("fired -- recorded; it is now a held memory.");
        return out;
    }

    std::unordered_set<std::string> observedIds;
    std::unordered_map<std::string, int> statLevels;
    const unlock::Knowledge k = makeKnowledge(state, growth, observedIds, statLevels);
    const bool sat = unlock::satisfied(r.unlock_when, k);

    // The roll this thought will face -- shown for EVERY status, so you can
    // plan even before it's reachable ("this will be a Reason d4 check").
    const int fac = growth::facultyLevel(growth, r.faculty);
    const int feed = feederBonus(r, growth);
    const int th = threshold(state, r);
    const std::string rollLine = "  roll: " + r.faculty + " " + std::to_string(fac) +
                                 (feed ? " + feeders " + std::to_string(feed) : "") +
                                 " + rand(0.." + std::to_string(state.roll.dice) +
                                 ") vs threshold " + std::to_string(th);

    if (!sat)
    {
        out.emplace_back("out of reach -- unmet entry conditions (any clause satisfies):");
        for (std::size_t i = 0; i < r.unlock_when.any.size(); ++i)
        {
            if (r.unlock_when.any.size() > 1)
                out.push_back("clause " + std::to_string(i + 1) + ":");
            explainClause(r.unlock_when.any[i], k, out);
        }
        out.emplace_back("then it will roll:");
        out.push_back(rollLine);
        return out;
    }

    const int floor = fac + feed;                 // roll floor (min nudge = 0)
    const int top = fac + feed + state.roll.dice; // roll ceiling (max nudge)
    out.emplace_back("available -- rolls whenever a relevant input next changes.");
    out.push_back(rollLine);
    out.push_back(std::string("  ") + (floor >= th ? "guaranteed pass"
                                       : top >= th ? "can pass on a good nudge"
                                                   : "cannot pass yet (grow the faculty/feeders)"));
    return out;
}

namespace
{
} // namespace

std::vector<std::string> explainCentrality(const State& state, const Thought& r)
{
    std::vector<std::string> out;
    // Mirror deriveCentrality: upstream = base worth of the transitive predecessor
    // web; downstream = opening of thoughts that transitively require this one.
    const std::unordered_set<std::string> up = upstreamIds(state, r);
    int upstream = 0;
    for (const auto& id : up)
        upstream += baseWorth(state, id);
    int downstream = 0;
    for (const auto& other : state.thoughts)
        if (other.id != r.id && upstreamIds(state, other).count(r.id))
            downstream += other.opening;

    out.push_back("centrality " + std::to_string(r.centrality) + " = upstream " +
                  std::to_string(upstream) + " + downstream " + std::to_string(downstream));
    if (up.empty())
        out.emplace_back("  upstream: (none -- a first thought)");
    else
    {
        out.emplace_back("  upstream (what led here, by base worth):");
        for (const auto& id : up)
        {
            const bool obs = observableById(state, id) != nullptr;
            out.push_back("    " + id + (obs ? " (obs value " : " (thought opening ") +
                          std::to_string(baseWorth(state, id)) + ")");
        }
    }
    if (downstream == 0)
        out.emplace_back("  downstream: (none -- terminal)");
    else
    {
        out.emplace_back("  downstream (thoughts that depend on this, by opening):");
        for (const auto& other : state.thoughts)
            if (other.id != r.id && upstreamIds(state, other).count(r.id))
                out.push_back("    " + other.id + " (opening " + std::to_string(other.opening) +
                              ")");
    }
    return out;
}

Signal signalFor(const State& state, const growth::GrowthState& /*growth*/, const std::string& spot)
{
    // The glimmer marks only "there is something here to look at." Once observed,
    // it quiets -- thoughts are the emergent layer and are not signposted.
    return state.observed_tier.count(spot) > 0 ? Signal::Observed : Signal::Unobserved;
}

} // namespace observations
