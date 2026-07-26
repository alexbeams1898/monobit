#include "Growth.h"
#include "Observations.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

using growth::GrowthState;
using observations::Encounter;
using observations::LineKind;
using observations::ObservationTier;
using observations::ObserveResult;
using observations::Outcome;
using observations::State;
using observations::Thought;

namespace
{
const observations::RollRng kNoNudge = [](int) { return 0; };    // roll = facultyLevel + feeders
const observations::RollRng kMaxNudge = [](int n) { return n; }; // + full dice

// Helper to build a stat clause.
unlock::Clause statClause(const std::string& f, int n)
{
    unlock::Clause c;
    c.stat[f] = n;
    return c;
}
unlock::Clause obsClause(std::vector<std::string> ids)
{
    unlock::Clause c;
    c.observed = std::move(ids);
    return c;
}

// A self with named stat levels.
GrowthState self(std::vector<std::pair<std::string, int>> stats)
{
    GrowthState g;
    g.faculties = {"wonder", "reason", "perception"};
    g.secondary = {"survival"};
    for (const auto& [name, lvl] : stats)
        g.stat_levels[name] = lvl;
    return g;
}

// A hand-built world (no config file -- these test the ENGINE, not authored
// content). Stone at (100,0): base tier + a moss tier gated at perception 3. A
// "moss_thought" thought (perception, needs stone observed). A "settlement"
// synthesis (reason, needs stone AND water observed, survival feeder). Water at
// (0,100). The trigger index is built here the same way load() does.
State makeWorld()
{
    State s;
    // You observe an observable within interact_reach of its box. These tests observe each
    // at its own center (obsAt helper), and the coords are distinct so each is unambiguous.
    Encounter stone;
    stone.id = "stone";
    stone.x = 100;
    stone.y = 0;
    stone.tiers = {
        ObservationTier{{}, "a stone", 5},
        ObservationTier{unlock::Condition{{statClause("perception", 3)}}, "a stone, mossy", 5},
    };
    Encounter water;
    water.id = "water";
    water.x = 0;
    water.y = 100;
    water.tiers = {ObservationTier{{}, "a dry channel", 5}};
    s.encounters = {stone, water};

    Thought moss;
    moss.id = "moss_thought";
    moss.unlock_when = unlock::Condition{{obsClause({"stone"})}}; // prereq = the observation only
    moss.faculty = "perception"; // perception WEIGHTS the roll (not a prereq)
    moss.difficulty = 2;         // threshold (2-1)*spread(3) = 3
    moss.text = "water shaped it once";
    moss.miss_text = "something about the stone you can't place";
    moss.spirit_exp = 10;

    Thought settlement;
    settlement.id = "settlement";
    settlement.unlock_when = unlock::Condition{{obsClause({"stone", "water"})}};
    settlement.faculty = "reason";
    settlement.difficulty = 4; // threshold (4-1)*3 = 9
    settlement.feeders = {{"survival", 2}};
    settlement.text = "people lived here";
    settlement.spirit_exp = 25;
    settlement.set_flag = "knows_settlement";
    s.thoughts = {moss, settlement};

    // Build the trigger index the way load() does.
    auto keysOf = [](const unlock::Condition& cond, std::vector<std::string>& out)
    {
        for (const auto& c : cond.any)
        {
            for (const auto& id : c.observed)
                out.push_back("obs:" + id);
            if (!c.flag.empty())
                out.push_back("flag:" + c.flag);
            for (const auto& [n, l] : c.stat)
                out.push_back("stat:" + n);
        }
    };
    std::vector<std::string> keys;
    for (std::size_t i = 0; i < s.thoughts.size(); ++i)
    {
        const auto& r = s.thoughts[i];
        keys.clear();
        keysOf(r.unlock_when, keys);
        if (!r.faculty.empty())
            keys.push_back("stat:" + r.faculty); // the roll faculty is indexed
        for (const auto& [feeder, per] : r.feeders)
            keys.push_back("stat:" + feeder); // feeders too
        for (const auto& k : keys)
            s.trigger_index[k].push_back(i);
    }
    return s;
}

// Observe the named observable by standing at its box center (within interact_reach).
ObserveResult obsAt(State& s, const GrowthState& g, const std::string& id,
                    const observations::RollRng& rng)
{
    for (const auto& o : s.encounters)
        if (o.id == id)
            return observations::observe(s, g, o.x, o.y, rng);
    return {Outcome::None, 0};
}
} // namespace

TEST_CASE("Objective tiers are deterministic: deepest met tier surfaces, no roll", "[observations]")
{
    State s = makeWorld();
    // Low perception: only the base tier.
    ObserveResult r = obsAt(s, self({}), "stone", kNoNudge);
    REQUIRE(r.outcome != Outcome::None);
    REQUIRE(s.pending.front().kind == LineKind::Observation);
    REQUIRE(s.pending.front().text == "a stone");
    REQUIRE(s.observed_tier.at("stone") == 1);
    s.pending.clear();

    // Grow perception to 3: the moss tier now surfaces (deterministic threshold).
    r = obsAt(s, self({{"perception", 3}}), "stone", kNoNudge);
    REQUIRE(s.pending.front().text == "a stone, mossy");
    REQUIRE(s.observed_tier.at("stone") == 2);
}

TEST_CASE("A thought rolls: misses at low faculty, lands once grown", "[observations]")
{
    // moss_thought: difficulty 2, faculty perception, no feeders. Self-scaling
    // threshold = frac(2) * capacityMax * tightness = 0.25 * (20+4) * 0.75 = 4.
    State s = makeWorld();
    // Observe the stone at perception 2: eligible (needs stone), rolls, but the
    // roll (2 + 0 nudge) < threshold 4 -> misses (nothing fires) BUT surfaces the
    // miss_text (failure is content -- the faint "something here you can't place").
    obsAt(s, self({{"perception", 2}}), "stone", kNoNudge);
    REQUIRE(s.fired.count("moss_thought") == 0);
    bool sawMiss = false;
    for (const auto& line : s.pending)
        if (line.text == "something about the stone you can't place")
            sawMiss = true;
    REQUIRE(sawMiss);
    s.pending.clear();

    // Re-observe the SAME (already-observed) spot at the SAME level: no obs: key
    // is pushed (best not > prevTier), so the engine doesn't even re-visit it --
    // no re-roll. Anti-abuse is structural (the trigger index), not a stored miss.
    obsAt(s, self({{"perception", 2}}), "stone", kNoNudge);
    REQUIRE(s.fired.count("moss_thought") == 0);

    // Grow perception to 4 and pump the stat change (what GameLoop does): the
    // ambient engine re-checks stat-keyed thoughts -> moss_thought re-rolls
    // (perception now weights the roll) -> 4 >= 4 -> lands. Generic re-open: ANY
    // relevant stat rise re-opens, via the index, not a per-thought flag.
    const ObserveResult r = observations::evaluateStats(s, self({{"perception", 4}}), kNoNudge);
    REQUIRE(s.fired.count("moss_thought") == 1);
    REQUIRE(r.outcome == Outcome::Thought);
    REQUIRE(r.earned == 10);
}

TEST_CASE("A synthesis needs a SERIES of observations and reads as synthesis", "[observations]")
{
    State s = makeWorld();
    // Observe stone only: settlement needs stone AND water -> not yet available.
    obsAt(s, self({{"reason", 20}}), "stone", kMaxNudge);
    REQUIRE(s.fired.count("settlement") == 0);

    // Observe water too -> now stone+water held; high reason lands the synthesis.
    obsAt(s, self({{"reason", 20}}), "water", kMaxNudge);
    REQUIRE(s.fired.count("settlement") == 1);
    REQUIRE(s.flags.count("knows_settlement") == 1); // yield applied
    // The settlement thought reads as a synthesis (gated on a SERIES: stone+water).
    const Thought* settlement = nullptr;
    for (const auto& t : s.thoughts)
        if (t.id == "settlement")
            settlement = &t;
    REQUIRE(settlement != nullptr);
    REQUIRE(observations::isSynthesis(*settlement));
}

TEST_CASE("Feeders add to the roll AND a feeder rise re-opens a missed synthesis", "[observations]")
{
    // settlement: difficulty 4, faculty reason, feeder survival:2. Self-scaling
    // threshold = frac(4) * capacityMax * tightness
    //           = 0.75 * (20 + 20/2 + 4) * 0.75 = 0.75 * 34 * 0.75 = 19.
    State s = makeWorld();
    // Observe both spots at reason 18, survival 0: eligible, but the roll
    // (18 + 0 feeders) < threshold 19 -> misses.
    obsAt(s, self({{"reason", 18}}), "water", kNoNudge); // water
    obsAt(s, self({{"reason", 18}}), "stone", kNoNudge); // stone -> rolls, misses
    REQUIRE(s.fired.count("settlement") == 0);

    // Raise ONLY the feeder (survival to 2 = +1 via survival:2). Reason is
    // unchanged. This must re-open the miss -- feeders are indexed, so the stat
    // pump re-checks it: 18 + 1 = 19 >= 19 -> lands. (This is the exact case the
    // old faculty-only re-open missed.)
    const ObserveResult r =
        observations::evaluateStats(s, self({{"reason", 18}, {"survival", 2}}), kNoNudge);
    REQUIRE(s.fired.count("settlement") == 1);
    REQUIRE(r.outcome == Outcome::Thought);
}

TEST_CASE("setFlag fires a flag-gated thought ambiently", "[observations]")
{
    State s;
    Thought r;
    r.id = "hears_the_bell";
    r.unlock_when = unlock::Condition{{unlock::Clause{}}};
    r.unlock_when.any[0].flag = "bell_rang";
    r.faculty = "wonder";
    r.difficulty = 1; // threshold 0 -> always lands
    r.text = "a bell, somewhere";
    r.spirit_exp = 3;
    s.thoughts = {r};
    s.trigger_index["flag:bell_rang"] = {0};

    ObserveResult res = observations::setFlag(s, self({}), "bell_rang", kNoNudge);
    REQUIRE(res.outcome == Outcome::Thought);
    REQUIRE(s.fired.count("hears_the_bell") == 1);
    // Setting the same flag again does nothing (already set).
    REQUIRE(observations::setFlag(s, self({}), "bell_rang", kNoNudge).outcome == Outcome::None);
}

TEST_CASE("Standing beyond interact_reach of every observable observes nothing", "[observations]")
{
    // Proximity: you must be within interact_reach of an observable's box. (500,500) is far
    // from both (stone at (100,0), water at (0,100)), so nothing resolves.
    State s = makeWorld();
    const ObserveResult r = observations::observe(s, self({}), 500, 500, kNoNudge);
    REQUIRE(r.outcome == Outcome::None);
    REQUIRE(s.pending.empty());
}

// --- VALUE derivation (load() runs deriveValues over the forward value graph) ---
namespace
{
// Write a config to a temp path, load it, remove the file. Exercises the real
// load() path so deriveValues() runs exactly as it does in the game.
State loadFromJson(const std::string& json)
{
    const std::string path = "value_derivation_test_tmp.json";
    State s;
    // These tests exercise the OBSERVATION half -- tiers, thoughts, the value graph -- and
    // load NO actions.json, so a fixture's `kind` resolves to no deeds here. But an Encounter
    // must offer BOTH halves to load (observe AND act). So give every fixture a stock deed
    // unless it already adds one itself. Keeps the invariant honest without making every
    // observation test spell out deeds it doesn't care about.
    nlohmann::json doc = nlohmann::json::parse(json);
    if (auto it = doc.find("encounters"); it != doc.end())
        for (auto& e : *it)
        {
            const auto acts = e.find("actions");
            const bool addsADeed =
                acts != e.end() && acts->contains("add") && !acts->at("add").empty();
            if (!addsADeed)
                e["actions"]["add"].push_back({{"id", "touch"}, {"label", "Touch it"}});
        }
    std::ofstream(path) << doc.dump();
    observations::load(s, path);
    std::remove(path.c_str());
    return s;
}

const Thought* findR(const State& s, const std::string& id)
{
    for (const auto& r : s.thoughts)
        if (r.id == id)
            return &r;
    return nullptr;
}
} // namespace

TEST_CASE("VALUE = observation value a thought sets in motion (forward graph)",
          "[observations][value]")
{
    // stone(1) + water(2) are always visible. people_lived_here fuses them and
    // sets knows_settlement, which reveals ruin(8). So firing it opens value 8
    // that wasn't reachable before -> VALUE 8. clearing_stillness opens nothing.
    const State s = loadFromJson(R"({
      "encounters": [
        { "id": "stone",    "value": 1, "x": 0, "y": 0,   "tiers": [{ "text": "a stone" }] },
        { "id": "water",    "value": 2, "x": 0, "y": 100, "tiers": [{ "text": "a channel" }] },
        { "id": "clearing", "value": 1, "x": 100,"y": 0,  "tiers": [{ "text": "a clearing" }] },
        { "id": "ruin",     "value": 8, "x": 0, "y": 200,
          "visible_when": [{ "flag": "knows_settlement" }],
          "tiers": [{ "text": "a ruin" }] }
      ],
      "thoughts": [
        { "id": "clearing_stillness", "unlock_when": [{ "observed": "clearing" }],
          "faculty": "wonder", "text": "still" },
        { "id": "people_lived_here", "unlock_when": [{ "observed": ["stone", "water"] }],
          "faculty": "reason", "feeders": { "survival": 2 },
          "text": "people", "set_flag": "knows_settlement" }
      ]
    })");

    const Thought* people = findR(s, "people_lived_here");
    const Thought* clearing = findR(s, "clearing_stillness");
    REQUIRE(people != nullptr);
    REQUIRE(clearing != nullptr);
    // people_lived_here reveals ruin(8) -> opening 8. Upstream = stone(1)+water(2)
    // -> centrality 3 -> importance 3. value = importance 3 + opening 8 = 11.
    REQUIRE(people->opening == 8);
    REQUIRE(people->centrality == 3);
    REQUIRE(people->value == 11);
    // clearing_stillness opens nothing (opening 0). Upstream = clearing(1) ->
    // centrality 1 -> importance 1. value = 1 -- a lonely thought still matters a
    // little, via what it stands on.
    REQUIRE(clearing->opening == 0);
    REQUIRE(clearing->value == 1);
    // Spirit EXP is derived from value (exp_per_value default 5).
    REQUIRE(people->spirit_exp == 11 * 5);
    REQUIRE(clearing->spirit_exp == 1 * 5);
}

TEST_CASE(
    "emotional_weight: a thought that MATTERS emotionally but opens nothing is still valuable",
    "[observations][value]")
{
    // The deathbed-confession case: `confession` reveals no observable (opening 0)
    // and is a structural dead-end (centrality only from its one upstream spot),
    // but authored emotional_weight 6 carries it. `discovery` reveals ruin(9) with
    // no emotion -- worth via the opening road. Both must land as valuable.
    const State s = loadFromJson(R"({
      "encounters": [
        { "id": "bedside", "value": 1, "x": 0,  "y": 0, "tiers": [{ "text": "bedside" }] },
        { "id": "trail",   "value": 1, "x": 50, "y": 0, "tiers": [{ "text": "a trail" }] },
        { "id": "ruin",    "value": 9, "x": 0,  "y": 80,
          "visible_when": [{ "flag": "found" }], "tiers": [{ "text": "a ruin" }] }
      ],
      "thoughts": [
        { "id": "confession", "unlock_when": [{ "observed": "bedside" }],
          "faculty": "reason", "emotional_weight": 6, "text": "he forgave me." },
        { "id": "discovery", "unlock_when": [{ "observed": "trail" }],
          "faculty": "perception", "text": "a path.", "set_flag": "found" }
      ]
    })");

    const Thought* confession = findR(s, "confession");
    const Thought* discovery = findR(s, "discovery");
    REQUIRE(confession != nullptr);
    REQUIRE(discovery != nullptr);
    // Emotion road: opening 0, centrality 1 (upstream bedside(1)), emotion 6 ->
    // importance = 1 + 6 = 7. value = 7 + 0 = 7.
    REQUIRE(confession->opening == 0);
    REQUIRE(confession->centrality == 1);
    REQUIRE(confession->importance == 7);
    REQUIRE(confession->value == 7);
    REQUIRE(confession->spirit_exp == 7 * 5);
    // Opening road: no emotion, centrality 1 (upstream trail(1)), reveals ruin(9)
    // -> importance 1 + opening 9 = value 10.
    REQUIRE(discovery->opening == 9);
    REQUIRE(discovery->value == 10);
}

TEST_CASE("Difficulty is STRUCTURAL (breadth + feeders), value only a quiet nudge",
          "[observations][value]")
{
    // A 1-observation thought with HUGE downstream value must still read as
    // low difficulty: "obvious major lore." Its structural load is 1 (breadth 1,
    // no feeders); the quiet value nudge (0.15 * value) can't lift it to the top.
    const State s = loadFromJson(R"({
      "encounters": [
        { "id": "clue", "value": 1, "x": 0, "y": 0, "tiers": [{ "text": "a clue" }] },
        { "id": "grand", "value": 40, "x": 0, "y": 100,
          "visible_when": [{ "flag": "seen" }], "tiers": [{ "text": "grand" }] }
      ],
      "thoughts": [
        { "id": "obvious_but_huge", "unlock_when": [{ "observed": "clue" }],
          "faculty": "perception", "text": "oh.", "set_flag": "seen" }
      ]
    })");

    const Thought* r = findR(s, "obvious_but_huge");
    REQUIRE(r != nullptr);
    // opening 40 (reveals grand) + importance 1 (centrality from clue(1)) = 41.
    REQUIRE(r->opening == 40);
    REQUIRE(r->value == 41); // huge consequence -> big reward
    // Difficulty stays LOW despite the huge value: structuralLoad 1 + a value nudge
    // capped at value_nudge_cap (1 band) -> not max_band. Value can't alone drive a
    // structurally simple thought to Legendary.
    REQUIRE(r->difficulty < s.roll.max_band);
}

TEST_CASE("Centrality: a terminal payoff (opens nothing) is valuable via UPSTREAM depth",
          "[observations][value]")
{
    // The who_left_here case. `payoff` opens nothing (terminal), but sits atop a
    // deep pyramid: it needs `middle` (which needs stone+water) AND `ruin`.
    // Its upstream base worth = stone(1)+water(2)+ruin(8) + middle's opening.
    // So a dead-end conclusion still reads as important -- much LED to it.
    const State s = loadFromJson(R"({
      "encounters": [
        { "id": "stone", "value": 1, "x": 0, "y": 0,   "tiers": [{ "text": "s" }] },
        { "id": "water", "value": 2, "x": 0, "y": 100, "tiers": [{ "text": "w" }] },
        { "id": "ruin",  "value": 8, "x": 0, "y": 200,
          "visible_when": [{ "flag": "settled" }], "tiers": [{ "text": "r" }] }
      ],
      "thoughts": [
        { "id": "middle", "unlock_when": [{ "observed": ["stone", "water"] }],
          "faculty": "reason", "text": "m", "set_flag": "settled" },
        { "id": "payoff", "unlock_when": [{ "observed": ["ruin", "middle"] }],
          "faculty": "reason", "text": "p" }
      ]
    })");

    const Thought* middle = findR(s, "middle");
    const Thought* payoff = findR(s, "payoff");
    REQUIRE(middle != nullptr);
    REQUIRE(payoff != nullptr);
    // payoff opens nothing (terminal) -> opening 0.
    REQUIRE(payoff->opening == 0);
    // But its upstream is the whole pyramid: stone(1)+water(2)+ruin(8) +
    // middle's own base worth (its opening = 8, it reveals ruin) = 19.
    REQUIRE(middle->opening == 8);
    REQUIRE(payoff->centrality == 1 + 2 + 8 + 8);
    // So the terminal payoff is valuable despite opening nothing: value =
    // importance(=centrality 19) + opening 0 = 19. The 0-value-payoff bug is fixed.
    REQUIRE(payoff->value == 19);
    REQUIRE(payoff->value > middle->value); // the culmination outweighs the hub here
}

TEST_CASE("A hidden observable can't be observed until its visible_when holds",
          "[observations][value]")
{
    State s = loadFromJson(R"({
      "encounters": [
        { "id": "ruin", "value": 8,
          "visible_when": [{ "flag": "knows_settlement" }], "tiers": [{ "text": "a ruin" }] }
      ],
      "thoughts": []
    })");

    const GrowthState g = self({});
    // Hidden -> observeById is a no-op (nothing surfaces).
    REQUIRE(observations::observeById(s, g, "ruin", kNoNudge).outcome == Outcome::None);
    // Reveal it via the flag, then observing it surfaces the reading.
    s.flags.insert("knows_settlement");
    REQUIRE(observations::observeById(s, g, "ruin", kNoNudge).outcome != Outcome::None);
}

// --- is_new: what the notebook writes on -----------------------------------------
// A reading carries is_new only the FIRST time it's reached. The notebook records on that
// flag alone (ThoughtBox::loadLine), so anything that sets it twice writes the same entry
// into the notebook twice.

TEST_CASE("a re-observed tier is not new the second time", "[observations][is_new]")
{
    State s = loadFromJson(R"({
      "encounters": [
        { "id": "rock", "kind": "inanimate", "x": 0, "y": 0,
          "tiers": [{ "text": "a rock, half-sunk" }] }
      ],
      "thoughts": []
    })");
    const GrowthState g = self({{"perception", 5}});

    // First look: the reading is new -- the notebook takes it.
    observations::observeById(s, g, "rock", kNoNudge);
    REQUIRE(s.pending.size() == 1);
    REQUIRE(s.pending.front().is_new);
    s.pending.clear(); // the box drains it

    // Every look after: the SAME reading surfaces, but it is not new. If this is true,
    // the notebook writes a duplicate every time the player walks past.
    observations::observeById(s, g, "rock", kNoNudge);
    REQUIRE(s.pending.size() == 1);
    REQUIRE_FALSE(s.pending.front().is_new);

    observations::observeById(s, g, "rock", kNoNudge);
    REQUIRE_FALSE(s.pending.front().is_new);
}

TEST_CASE("only the newly-reached tier is new, not the ones below it", "[observations][is_new]")
{
    State s = loadFromJson(R"({
      "encounters": [
        { "id": "rock", "kind": "inanimate", "x": 0, "y": 0,
          "tiers": [
            { "text": "a rock" },
            { "text": "moss on its north face", "unlock_when": [{ "stat": { "perception": 3 } }] }
          ] }
      ],
      "thoughts": []
    })");

    // Shallow look reaches tier 1 only.
    observations::observeById(s, self({{"perception", 1}}), "rock", kNoNudge);
    REQUIRE(s.pending.front().is_new);
    REQUIRE(s.pending.front().text == "a rock");
    s.pending.clear();

    // Deeper perception reaches tier 2: new again, because it's a tier never reached --
    // a second notebook entry, and correctly so (it's a different reading).
    observations::observeById(s, self({{"perception", 5}}), "rock", kNoNudge);
    REQUIRE(s.pending.front().is_new);
    REQUIRE(s.pending.front().text == "moss on its north face");
    s.pending.clear();

    // But looking again at that same depth is not new.
    observations::observeById(s, self({{"perception", 5}}), "rock", kNoNudge);
    REQUIRE_FALSE(s.pending.front().is_new);
}

TEST_CASE("observing REPORTS the thoughts that landed, by id", "[observations][landed]")
{
    // The result's `landed` is the only way a thought reaches the notebook: the game writes
    // down exactly what this reports. Firing a thought but reporting nothing means the
    // pilgrim thinks it and never records it -- invisible, because `pending` (the reading on
    // screen) is populated from a different path and still looks right.
    State s = loadFromJson(R"({
      "encounters": [
        { "id": "rock", "kind": "inanimate", "x": 0, "y": 0,
          "tiers": [{ "text": "a rock" }] }
      ],
      "thoughts": [
        { "id": "rock_thought", "text": "it was shaped by water",
          "faculty": "wonder", "value": 1,
          "unlock_when": [{ "observed": "rock" }] }
      ]
    })");
    const GrowthState g = self({{"wonder", 20}, {"perception", 5}});

    const ObserveResult r = observations::observeById(s, g, "rock", kMaxNudge);
    REQUIRE(r.landed == std::vector<std::string>{"rock_thought"});
    REQUIRE(s.fired.count("rock_thought") == 1); // and the record agrees with the report

    // Re-observing lands nothing new -- so nothing is reported, and no second note is written.
    REQUIRE(observations::observeById(s, g, "rock", kMaxNudge).landed.empty());
}

TEST_CASE("a landed thought is never reported as a granted item", "[observations][landed]")
{
    // ObserveResult carries several id lists; `landed` is not the first of them. A positional
    // init would load thought ids into `granted` and the game would deposit them as items.
    State s = loadFromJson(R"({
      "encounters": [
        { "id": "rock", "kind": "inanimate", "x": 0, "y": 0,
          "tiers": [{ "text": "a rock" }] }
      ],
      "thoughts": [
        { "id": "rock_thought", "text": "it was shaped by water",
          "faculty": "wonder", "value": 1,
          "unlock_when": [{ "observed": "rock" }] }
      ]
    })");
    const ObserveResult r =
        observations::observeById(s, self({{"wonder", 20}, {"perception", 5}}), "rock", kMaxNudge);
    REQUIRE_FALSE(r.landed.empty());
    REQUIRE(r.granted.empty()); // a thought is not a thing you can put in a satchel
    REQUIRE(r.gathered.empty());
    REQUIRE(r.taught.empty());
}

TEST_CASE("walking into an ambient spot reports its thoughts too", "[observations][landed]")
{
    // The enter-trigger path shares observeEncounter with the deliberate verb, and accumulates
    // across every spot that came within reach this frame -- a walk past two of them must
    // report both, not just the last.
    State s = loadFromJson(R"({
      "encounters": [
        { "id": "grove", "kind": "place", "tiers": [{ "text": "a grove" }] },
        { "id": "brook", "kind": "water", "tiers": [{ "text": "a brook" }] }
      ],
      "thoughts": [
        { "id": "grove_thought", "text": "held breath", "faculty": "wonder", "value": 1,
          "unlock_when": [{ "observed": "grove" }] },
        { "id": "brook_thought", "text": "still running", "faculty": "wonder", "value": 1,
          "unlock_when": [{ "observed": "brook" }] }
      ]
    })");
    // The box + trigger are placement, which the map supplies (not the observations config).
    // placement_id marks each as PLACED in the current level -- an unplaced encounter is
    // elsewhere in the world and proximity skips it.
    for (auto& o : s.encounters)
    {
        o.trigger = observations::Trigger::Enter;
        o.placement_id = "p_" + o.id;
        o.x = 0;
        o.y = 0;
    }
    const ObserveResult r = observations::triggerProximity(
        s, self({{"wonder", 20}, {"perception", 5}}), 0, 0, kMaxNudge);
    REQUIRE(r.landed.size() == 2);
    REQUIRE(s.fired.count("grove_thought") == 1);
    REQUIRE(s.fired.count("brook_thought") == 1);
}

TEST_CASE("a thought fires -- and is new -- exactly once", "[observations][is_new]")
{
    State s = loadFromJson(R"({
      "encounters": [
        { "id": "rock", "kind": "inanimate", "x": 0, "y": 0,
          "tiers": [{ "text": "a rock" }] }
      ],
      "thoughts": [
        { "id": "rock_thought", "text": "it was shaped by water",
          "faculty": "wonder", "value": 1,
          "unlock_when": [{ "observed": "rock" }] }
      ]
    })");
    const GrowthState g = self({{"wonder", 20}, {"perception", 5}});

    // Observing fires the thought once.
    observations::observeById(s, g, "rock", kMaxNudge);
    int newThoughts = 0;
    for (const auto& p : s.pending)
        if (p.kind == LineKind::Thought && p.is_new)
            ++newThoughts;
    REQUIRE(newThoughts == 1);
    s.pending.clear();

    // Re-observing must not fire it again: `fired` already holds it, so the notebook
    // gets no second copy.
    observations::observeById(s, g, "rock", kMaxNudge);
    for (const auto& p : s.pending)
        REQUIRE(p.kind != LineKind::Thought);
}

// --- Actions (kind defaults + per-spot overrides; takeAction -> ambient) ------
namespace
{
// Load with BOTH an observations json and an actions (kinds) json, the way the
// game does. Exercises resolveActions (kind defaults merged with overrides).
State loadWithActions(const std::string& obs, const std::string& actions)
{
    const std::string obsPath = "actions_test_obs_tmp.json";
    const std::string actPath = "actions_test_act_tmp.json";
    {
        std::ofstream(obsPath) << obs;
    }
    {
        std::ofstream(actPath) << actions;
    }
    State s;
    observations::load(s, obsPath, actPath);
    std::remove(obsPath.c_str());
    std::remove(actPath.c_str());
    return s;
}

const observations::Encounter* findObs(const State& s, const std::string& id)
{
    for (const auto& o : s.encounters)
        if (o.id == id)
            return &o;
    return nullptr;
}
const std::string kKinds = R"({
  "action_kinds": {
    "inanimate": { "actions": [
      { "id": "search",    "label": "Search",  "one_shot": true },
      { "id": "rest_hand", "label": "Touch",   "one_shot": false }
    ]}
  }
})";
} // namespace

TEST_CASE("Actions resolve at load: kind defaults + add/remove/replace overrides", "[actions]")
{
    const State s = loadWithActions(R"({
      "encounters": [
        { "id": "plain", "kind": "inanimate", "x": 0, "y": 0, "tiers": [{ "text": "t" }] },
        { "id": "fancy", "kind": "inanimate", "x": 0, "y": 0, "tiers": [{ "text": "t" }],
          "actions": {
            "remove":  ["rest_hand"],
            "replace": [{ "id": "search", "label": "Rummage" }],
            "add":     [{ "id": "clear_moss", "label": "Clear moss", "set_flag": "cleared" }]
          } }
      ],
      "thoughts": []
    })",
                                    kKinds);

    const observations::Encounter* plain = findObs(s, "plain");
    const observations::Encounter* fancy = findObs(s, "fancy");
    REQUIRE(plain != nullptr);
    REQUIRE(fancy != nullptr);
    // plain inherits both kind defaults.
    REQUIRE(plain->actions.size() == 2);
    // fancy: removed rest_hand, replaced search's label, added clear_moss.
    REQUIRE(fancy->actions.size() == 2);
    bool sawRummage = false;
    bool sawClearMoss = false;
    bool sawRestHand = false;
    for (const auto& a : fancy->actions)
    {
        if (a.id == "search")
            sawRummage = (a.label == "Rummage");
        if (a.id == "clear_moss")
            sawClearMoss = true;
        if (a.id == "rest_hand")
            sawRestHand = true;
    }
    REQUIRE(sawRummage);
    REQUIRE(sawClearMoss);
    REQUIRE_FALSE(sawRestHand);
}

TEST_CASE("availableActions filters by unlock_when and one_shot-taken", "[actions]")
{
    State s = loadWithActions(R"({
      "encounters": [
        { "id": "stone", "kind": "inanimate", "x": 0, "y": 0, "tiers": [{ "text": "t" }],
          "actions": { "add": [
            { "id": "gated", "label": "Pry",
              "unlock_when": [{ "stat": { "perception": 3 } }], "one_shot": true } ] } }
      ],
      "thoughts": []
    })",
                              kKinds);

    // At perception 1: search + rest_hand offered, gated NOT (needs perception 3).
    REQUIRE(observations::availableActions(s, self({{"perception", 1}}), "stone").size() == 2);
    // At perception 3: gated is now offered too -> 3.
    const GrowthState g3 = self({{"perception", 3}});
    REQUIRE(observations::availableActions(s, g3, "stone").size() == 3);
    // Take the one_shot search -> it drops off the menu. `taken` is keyed by SPOT:id, so the
    // deed is tracked per-spot (a deed id shared across spots isn't marked taken everywhere).
    observations::takeAction(s, g3, "stone", "search", kNoNudge);
    REQUIRE(s.taken.count("stone:search") == 1);
    const auto after = observations::availableActions(s, g3, "stone");
    REQUIRE(after.size() == 2); // search gone; rest_hand (repeatable) + gated remain
    for (const auto* a : after)
        REQUIRE(a->id != "search");
}

TEST_CASE("A one-shot deed taken at one spot stays available at another spot", "[actions]")
{
    // Two spots share the same kind-default deed id ("search"). Taking it at one must NOT mark
    // it taken at the other -- caches/yields depend on this per-spot scoping.
    State s = loadWithActions(R"({
      "encounters": [
        { "id": "cairn_a", "kind": "inanimate", "x": 0, "y": 0, "tiers": [{ "text": "a" }] },
        { "id": "cairn_b", "kind": "inanimate", "x": 99, "y": 0, "tiers": [{ "text": "b" }] }
      ],
      "thoughts": []
    })",
                              kKinds);

    const GrowthState g = self({{"perception", 1}});
    // search is a one_shot inanimate default; both spots offer it up front.
    const auto beforeA = observations::availableActions(s, g, "cairn_a");
    const auto beforeB = observations::availableActions(s, g, "cairn_b");
    const bool aHasSearch = std::any_of(beforeA.begin(), beforeA.end(),
                                        [](const auto* x) { return x->id == "search"; });
    const bool bHasSearch = std::any_of(beforeB.begin(), beforeB.end(),
                                        [](const auto* x) { return x->id == "search"; });
    REQUIRE(aHasSearch);
    REQUIRE(bHasSearch);

    // Take search at A only.
    observations::takeAction(s, g, "cairn_a", "search", kNoNudge);
    REQUIRE(s.taken.count("cairn_a:search") == 1);
    REQUIRE(s.taken.count("cairn_b:search") == 0);

    // A no longer offers search; B still does (untouched).
    const auto afterA = observations::availableActions(s, g, "cairn_a");
    const auto afterB = observations::availableActions(s, g, "cairn_b");
    REQUIRE(std::none_of(afterA.begin(), afterA.end(),
                         [](const auto* x) { return x->id == "search"; }));
    REQUIRE(
        std::any_of(afterB.begin(), afterB.end(), [](const auto* x) { return x->id == "search"; }));
}

TEST_CASE("takeAction recovers a thought that observing alone couldn't reach", "[actions]")
{
    // chisel_marks needs the stone observed AND the moss cleared. Observing the
    // stone alone can't land it; the clear_moss ACTION sets the flag -> the ambient
    // engine fires it. This is the fail/locked-thought recovery path.
    State s = loadWithActions(R"({
      "encounters": [
        { "id": "stone", "kind": "inanimate", "x": 0, "y": 0,
          "tiers": [{ "text": "a stone" }],
          "actions": { "add": [
            { "id": "clear_moss", "label": "Clear the moss",
              "unlock_when": [{ "observed": "stone" }],
              "result_text": "The moss comes away.",
              "set_flag": "moss_cleared", "one_shot": true } ] } }
      ],
      "thoughts": [
        { "id": "chisel_marks",
          "unlock_when": [{ "observed": "stone", "flag": "moss_cleared" }],
          "faculty": "perception", "text": "chisel marks" }
      ]
    })",
                              kKinds);

    const GrowthState g = self({{"perception", 20}});
    // Observe the stone (east, at x=100): chisel_marks needs the flag too -> no fire.
    observations::observe(s, g, 0, 0, kMaxNudge);
    REQUIRE(s.fired.count("chisel_marks") == 0);
    // clear_moss is offered (stone observed); taking it sets moss_cleared and the
    // ambient engine now lands chisel_marks.
    const ObserveResult r = observations::takeAction(s, g, "stone", "clear_moss", kMaxNudge);
    REQUIRE(s.flags.count("moss_cleared") == 1);
    REQUIRE(s.fired.count("chisel_marks") == 1);
    REQUIRE(r.outcome == Outcome::Thought);
    // The deed's own voice surfaced too (result_text as a plain line).
    bool sawResult = false;
    for (const auto& line : s.pending)
        if (line.text == "The moss comes away.")
            sawResult = true;
    REQUIRE(sawResult);
}

TEST_CASE("A deed with grant_item / grant_table / grant_recipe returns those ids for the game",
          "[actions]")
{
    // Deeds on an observable-and-takeable stone. observations is inventory/recipe-ignorant:
    // takeAction returns the opaque ids; the GAME does the deposit / teaches the recipe.
    State s = loadWithActions(R"({
      "encounters": [
        { "id": "stone", "kind": "inanimate", "x": 0, "y": 0,
          "tiers": [{ "text": "a smooth stone" }],
          "actions": { "add": [
            { "id": "take", "label": "Pick it up", "grant_item": "river_stone",
              "one_shot": true },
            { "id": "forage", "label": "Forage nearby", "grant_table": "herbs" },
            { "id": "read", "label": "Read the lichen", "grant_recipe": "herbal_draught" } ] } }
      ],
      "thoughts": []
    })",
                              kKinds);

    const GrowthState g = self({{"perception", 5}});
    const ObserveResult take = observations::takeAction(s, g, "stone", "take", kNoNudge);
    REQUIRE(take.granted == std::vector<std::string>{"river_stone"});
    REQUIRE(take.gathered.empty());
    REQUIRE(take.taught.empty());
    REQUIRE(take.consumed_spot.empty()); // "forage nearby" doesn't consume; default false

    const ObserveResult forage = observations::takeAction(s, g, "stone", "forage", kNoNudge);
    REQUIRE(forage.gathered == std::vector<std::string>{"herbs"});
    REQUIRE(forage.granted.empty());

    // A grant_recipe deed surfaces the recipe id in `taught` for the game to learn.
    const ObserveResult read = observations::takeAction(s, g, "stone", "read", kNoNudge);
    REQUIRE(read.taught == std::vector<std::string>{"herbal_draught"});
    REQUIRE(read.granted.empty());
    REQUIRE(read.gathered.empty());
}

TEST_CASE("A consumes_spot take reports the spot id for the game to despawn", "[actions]")
{
    // "Pick up the whole pebble" removes the observable; "take a sample" leaves it. Only the
    // consuming deed reports consumed_spot -- the game removes that world entity.
    State s = loadWithActions(R"({
      "encounters": [
        { "id": "pebble", "kind": "inanimate", "x": 0, "y": 0,
          "tiers": [{ "text": "a small pebble" }],
          "actions": { "add": [
            { "id": "pocket", "label": "Pocket the pebble", "grant_item": "river_stone",
              "consumes_spot": true, "one_shot": true },
            { "id": "brush", "label": "Brush it off" } ] } }
      ],
      "thoughts": []
    })",
                              kKinds);

    const GrowthState g = self({{"perception", 5}});
    const ObserveResult brush = observations::takeAction(s, g, "pebble", "brush", kNoNudge);
    REQUIRE(brush.consumed_spot.empty()); // a non-consuming deed leaves the spot

    const ObserveResult pocket = observations::takeAction(s, g, "pebble", "pocket", kNoNudge);
    REQUIRE(pocket.granted == std::vector<std::string>{"river_stone"});
    REQUIRE(pocket.consumed_spot == "pebble"); // the game despawns this observable's entity
}

TEST_CASE("availableUnlocks: a deeper tier becomes reachable after growth (the pull-back)",
          "[actions][notify]")
{
    // stone has a base tier + a moss tier gated at perception 3. Observe it at
    // perception 1 (only the base surfaces). Growing perception to 3 makes the
    // moss tier reachable-but-unobserved -> it shows up as "stone@2".
    State s = loadFromJson(R"({
      "encounters": [
        { "id": "stone", "value": 1, "x": 0, "y": 0,
          "tiers": [
            { "text": "a stone" },
            { "unlock_when": [{ "stat": { "perception": 3 } }], "text": "mossy" }
          ] }
      ],
      "thoughts": []
    })");

    // Observe at perception 1: only the base tier reached; no deeper tier yet.
    observations::observe(s, self({{"perception", 1}}), 0, 0, kNoNudge);
    REQUIRE(observations::availableUnlocks(s, self({{"perception", 1}})).count("stone@2") == 0);
    // Grow perception to 3: the moss tier's gate is now met but unobserved -> pull.
    REQUIRE(observations::availableUnlocks(s, self({{"perception", 3}})).count("stone@2") == 1);
    // Observing it (going back) consumes it -> no longer in the set.
    observations::observe(s, self({{"perception", 3}}), 0, 0, kNoNudge);
    REQUIRE(observations::availableUnlocks(s, self({{"perception", 3}})).count("stone@2") == 0);
}

TEST_CASE("availableUnlocks: actions announce only for OBSERVED spots; hidden spots never",
          "[actions][notify]")
{
    State s = loadWithActions(R"({
      "encounters": [
        { "id": "stone", "kind": "inanimate", "x": 0, "y": 0,
          "tiers": [{ "text": "s" }] },
        { "id": "ruin",  "kind": "inanimate", "x": 200, "y": 0,
          "visible_when": [{ "flag": "found" }], "tiers": [{ "text": "r" }] }
      ],
      "thoughts": []
    })",
                              kKinds);

    const GrowthState g = self({});
    // Unobserved stone: its actions are NOT announced (glimmer discovers it, not
    // a notification).
    REQUIRE(observations::availableUnlocks(s, g).count("stone:search") == 0);
    // Observe the stone -> now its offered actions announce as pull-backs.
    observations::observe(s, g, 0, 0, kNoNudge);
    const auto unlocks = observations::availableUnlocks(s, g);
    REQUIRE(unlocks.count("stone:search") == 1);
    REQUIRE(unlocks.count("stone:rest_hand") == 1);
    // ruin is hidden (visible_when unmet) -> never announced.
    REQUIRE(unlocks.count("ruin:search") == 0);
}

// --- placement binding (location comes from the map, content from JSON) ------

namespace
{
State loadForPlacement()
{
    // Content only -- no x/y/w/h/trigger here; placement is the map's job now.
    return loadFromJson(R"({
      "encounters": [
        { "id": "stone", "kind": "inanimate", "tiers": [{ "text": "a boulder" }] },
        { "id": "river", "kind": "water",     "tiers": [{ "text": "the water" }] }
      ],
      "thoughts": []
    })");
}
} // namespace

TEST_CASE("applyPlacements binds map location + trigger onto loaded observation content",
          "[observations][placement]")
{
    State s = loadForPlacement();
    const std::vector<observations::Placement> places = {
        {"stone", "p_stone", 976.0f, 656.0f, 64.0f, 64.0f, observations::Trigger::Observe},
        {"river", "p_river", 464.0f, 400.0f, 128.0f, 128.0f, observations::Trigger::Enter},
    };
    const auto rep = observations::applyPlacements(s, places);
    REQUIRE(rep.placements_without_encounter.empty());
    REQUIRE(rep.encounters_without_placement.empty());

    const Encounter* stone = nullptr;
    const Encounter* river = nullptr;
    for (const auto& o : s.encounters)
    {
        if (o.id == "stone")
            stone = &o;
        if (o.id == "river")
            river = &o;
    }
    REQUIRE(stone != nullptr);
    REQUIRE(river != nullptr);

    // The placement's IDENTITY binds too, not just its box: it's what the game records a
    // consumed spot against, so a spot removed for good doesn't come back from the map.
    REQUIRE(stone->placement_id == "p_stone");
    REQUIRE(river->placement_id == "p_river");
    REQUIRE(stone->x == 976.0f);
    REQUIRE(stone->y == 656.0f);
    REQUIRE(stone->w == 64.0f);
    REQUIRE(stone->trigger == observations::Trigger::Observe);
    REQUIRE(river->trigger == observations::Trigger::Enter); // area, ambient
    REQUIRE(river->w == 128.0f);
}

TEST_CASE("applyPlacements reports authoring gaps both ways", "[observations][placement]")
{
    State s = loadForPlacement(); // has stone + river
    // A placement for content that doesn't exist, and river left unplaced.
    const std::vector<observations::Placement> places = {
        {"stone", "p_stone", 10.0f, 20.0f, 48.0f, 48.0f, observations::Trigger::Observe},
        {"ghost", "p_ghost", 0.0f, 0.0f, 48.0f, 48.0f,
         observations::Trigger::Observe}, // no such observable
    };
    const auto rep = observations::applyPlacements(s, places);
    REQUIRE(rep.placements_without_encounter.size() == 1);
    REQUIRE(rep.placements_without_encounter[0] == "ghost");
    REQUIRE(rep.encounters_without_placement.size() == 1);
    REQUIRE(rep.encounters_without_placement[0] == "river"); // content with no location
}

// --- ambient proximity triggers (enter/approach) -----------------------------

TEST_CASE("An ENTER observable fires once when the player reaches its box, not before",
          "[observations][trigger]")
{
    State s = loadForPlacement();
    // A big river box (100x100 at (400,0)) -> its left edge is x=350.
    observations::applyPlacements(
        s, {{"river", "p_river", 400.0f, 0.0f, 100.0f, 100.0f, observations::Trigger::Enter}});
    const GrowthState g = self({});

    // Far away -> nothing fires.
    auto r = observations::triggerProximity(s, g, 0.0f, 0.0f, kNoNudge);
    REQUIRE(r.outcome == Outcome::None);

    // Within reach of the box -> it fires (a reading surfaces).
    r = observations::triggerProximity(s, g, 350.0f, 0.0f, kNoNudge);
    REQUIRE(r.outcome != Outcome::None);

    // Still near next frame -> does NOT fire again (edge-triggered on `fired`).
    r = observations::triggerProximity(s, g, 360.0f, 0.0f, kNoNudge);
    REQUIRE(r.outcome == Outcome::None);
}

TEST_CASE("Proximity triggering ignores OBSERVE-mode encounters (they need the verb)",
          "[observations][trigger]")
{
    State s = loadForPlacement();
    // stone is Observe-mode: standing on it must NOT auto-fire.
    observations::applyPlacements(
        s, {{"stone", "p_stone", 0.0f, 0.0f, 100.0f, 100.0f, observations::Trigger::Observe}});
    const GrowthState g = self({});
    const auto r = observations::triggerProximity(s, g, 0.0f, 0.0f, kNoNudge);
    REQUIRE(r.outcome == Outcome::None); // observe-mode is silent to proximity
}

TEST_CASE("an Encounter must offer BOTH halves -- observe AND act", "[observations][invariant]")
{
    // An Encounter is a glowing spot you can observe OR act on. The two are independent at
    // play time, but a spot that authors only one half is a content gap: a reading-less spot
    // can't be observed, a deed-less spot can't be acted on. Both are dropped at load. (The
    // harness normally injects a stock deed -- these fixtures opt out to exercise the rule.)

    // loadFromJson injects a stock deed so observation tests need not author one; these test
    // the un-helped rule, so they load the raw JSON directly.
    auto loadRaw = [](const char* json)
    {
        const std::string path = "encounter_invariant_test.tmp.json";
        std::ofstream(path) << json;
        State s;
        observations::load(s, path);
        std::remove(path.c_str());
        return s;
    };

    SECTION("a reading-less Encounter is dropped")
    {
        const State s = loadRaw(R"({
          "encounters": [
            { "id": "no_reading", "x": 0, "y": 0,
              "actions": { "add": [{ "id": "touch", "label": "Touch" }] } }
          ]
        })");
        REQUIRE(s.encounters.empty());
    }

    SECTION("a deed-less Encounter is dropped")
    {
        const State s = loadRaw(R"({
          "encounters": [
            { "id": "no_deed", "x": 0, "y": 0,
              "tiers": [{ "text": "a plain thing" }] }
          ]
        })");
        REQUIRE(s.encounters.empty());
    }

    SECTION("an Encounter with both halves loads")
    {
        const State s = loadRaw(R"({
          "encounters": [
            { "id": "whole", "x": 0, "y": 0,
              "tiers": [{ "text": "a plain thing" }],
              "actions": { "add": [{ "id": "touch", "label": "Touch" }] } }
          ]
        })");
        REQUIRE(s.encounters.size() == 1);
        REQUIRE(s.encounters.front().id == "whole");
    }
}

TEST_CASE("visible() gates a hidden Encounter -- the one answer observe + act share",
          "[observations][invariant]")
{
    // An Encounter hidden by visible_when must offer NEITHER verb. Before the fix, observe
    // checked visibility and act didn't, so they disagreed (a hidden log: act opened a menu,
    // observe showed nothing). visible() is now the single gate -- the interactable's presence
    // reads it, so a hidden spot isn't a target at all.
    const State s = loadFromJson(R"({
      "encounters": [
        { "id": "shown",  "x": 0, "y": 0, "tiers": [{ "text": "here" }] },
        { "id": "hidden", "x": 0, "y": 100,
          "visible_when": [{ "flag": "revealed" }],
          "tiers": [{ "text": "there" }] }
      ]
    })");
    const GrowthState g = self({});

    REQUIRE(observations::visible(s, g, "shown"));          // no gate -> always present
    REQUIRE_FALSE(observations::visible(s, g, "hidden"));   // flag unmet -> not present
    REQUIRE_FALSE(observations::visible(s, g, "nonesuch")); // unknown id -> not present

    // observeById agrees with visible(): a hidden spot surfaces nothing.
    const ObserveResult r = observations::observeById(const_cast<State&>(s), g, "hidden", kNoNudge);
    REQUIRE(r.outcome == Outcome::None);
}

TEST_CASE("a revealed Encounter becomes visible the moment its flag is set",
          "[observations][invariant]")
{
    // Live, not one-time: forming the conclusion that sets the flag makes the spot present.
    State s = loadFromJson(R"({
      "encounters": [
        { "id": "log", "x": 0, "y": 0,
          "visible_when": [{ "flag": "knows_settlement" }],
          "tiers": [{ "text": "a fallen beam" }] }
      ]
    })");
    const GrowthState g = self({});

    REQUIRE_FALSE(observations::visible(s, g, "log")); // hidden at first
    s.flags.insert("knows_settlement");
    REQUIRE(observations::visible(s, g, "log")); // the same query now says present
}

TEST_CASE("a landed thought reports faculty EXP for passive stat growth", "[observations][growth]")
{
    // Passive growth: when a thought lands, the result carries (its faculty, stat_exp) so the
    // game grows that stat. stat_exp derives from value like spirit_exp (rarity/tier fold in).
    State s = loadFromJson(R"({
      "roll": { "stat_exp_per_value": 3 },
      "encounters": [
        { "id": "rock", "value": 4, "x": 0, "y": 0, "tiers": [{ "text": "a rock" }] },
        { "id": "ruin", "value": 8, "x": 0, "y": 200,
          "visible_when": [{ "flag": "knows_it" }], "tiers": [{ "text": "a ruin" }] }
      ],
      "thoughts": [
        { "id": "rock_thought", "text": "shaped by water", "faculty": "wonder",
          "set_flag": "knows_it",
          "unlock_when": [{ "observed": "rock" }] }
      ]
    })");
    const GrowthState g = self({{"wonder", 20}, {"perception", 5}});

    const ObserveResult r = observations::observeById(const_cast<State&>(s), g, "rock", kMaxNudge);
    REQUIRE(s.fired.count("rock_thought") == 1);

    // The thought grew its OWN faculty (wonder), by value * stat_exp_per_value.
    bool grewWonder = false;
    for (const auto& [faculty, exp] : r.stat_gains)
        if (faculty == "wonder")
        {
            grewWonder = true;
            REQUIRE(exp > 0); // derived from the thought's value
        }
    REQUIRE(grewWonder);
    // No spurious growth of a faculty the thought didn't exercise.
    for (const auto& [faculty, exp] : r.stat_gains)
        REQUIRE(faculty != "reason");
}
