#pragma once

#include "Psyche.h"
#include "UnlockCondition.h"

#include <nlohmann/json_fwd.hpp>

#include <string>
#include <unordered_set>
#include <vector>

// Story arcs -- a thread. An arc is one goal flag reached by any of several ROUTES, so a
// looker, a reasoner and a maker can arrive at the same understanding by different means.
// Routes are `unlock::Condition`s, so an arc introduces no new gating concept: the world
// already holds the combinatorics (docs/design/GAME-SYSTEMS.md).
//
// The ROUTES are authoring apparatus -- nothing in the game loop consults them; play runs off
// flags exactly as it does without an arc file, and they exist so a thread can be seen whole
// and CHECKED (a renamed flag that quietly orphans a route is caught at load, not in
// playtest). What an arc additionally carries is the thread's PLAYER-FACING half: the line he
// writes in his agenda, when he learns to write it, and the hours the world is willing to
// receive it. Delete the line and the arc goes back to being invisible scaffolding.
//
// Pure over its inputs -- unit-testable. See docs/design/MAP-ARCHITECTURE.md section 6
// ("quests are an authoring pattern, not a subsystem").
namespace arcs
{

// One way through an arc: a label for the author and the condition that satisfies it.
struct Route
{
    std::string label; // authoring only -- never player-facing
    unlock::Condition when;
};

// A named thread: several routes converging on one goal flag.
struct Arc
{
    std::string id;
    std::string goal_flag;
    std::vector<Route> routes;

    // The agenda half. `line` is what he has written down -- absent, the arc stays pure
    // scaffolding and never surfaces. `known_when` is when he learns it (unconditional = he
    // began the walk knowing). `window` is the hours and days the world will receive it: a
    // shut window never fails the thread, it only says "not now" (see openness).
    std::string line;
    unlock::Condition known_when;
    unlock::Condition window;
};

struct Registry
{
    std::vector<Arc> arcs;
};

// Load arcs from config/arcs.json. Missing/unparseable -> empty registry (the permissive
// config policy). Malformed entries are dropped with a warning, like an encounter with no
// readings.
void load(Registry& out, const std::string& path);

// What the world can actually produce, assembled by the caller from the loaded content. A
// route referencing anything absent here can never be satisfied.
struct Producible
{
    std::unordered_set<std::string> observable; // encounter + thought ids that can be observed
    // Flags anything can RAISE. Deeds and thoughts supply most; a clearing on the map supplies
    // the rest (the last pile hauled off a path raises its flag), so the caller adds those --
    // otherwise a goal the WORLD can reach reads here as one nothing can.
    std::unordered_set<std::string> flags;
    std::unordered_set<std::string> read_flags; // flags something actually gates on
};

// An authoring mistake found in the arc graph. `arc` is the arc's id; `detail` names the
// offending route/flag. Severity is deliberately absent -- every finding here is a bug in the
// authored story, and they are all reported the same way.
struct Problem
{
    std::string arc;
    std::string detail;
};

// Survey the loaded observation content: what memories it can produce, what flags its deeds
// and thoughts set, and what flags anything actually gates on. The bridge between authored
// content and the check below -- declared here (rather than in Observations) so the arc
// linter stays self-contained and the observation engine keeps no knowledge of arcs.
Producible survey(const psyche::State& state);

// --- The agenda: what he owes, read off the same flags the world already runs on ----------
//
// No state. An arc is on the agenda when he has learned it and has not finished it, and that
// is entirely a question about flags -- so there is nothing to save, nothing to keep in sync,
// and no "accepted"/"active"/"failed" bit that could disagree with the world.

// Whether the world will receive this thread right now, and if not, whether waiting helps.
enum class Openness
{
    Open,      // its window holds now (or it has no window at all)
    ShutUntil, // shut now, but it opens again later -- "his door is shut until morning"
    ShutToday, // shut for the rest of today; a new day is what reopens it
};

// One line of the agenda.
struct Item
{
    const Arc* arc = nullptr; // into the loaded registry, never a copy
    Openness openness = Openness::Open;
    double opens_at = -1.0; // day-fraction the window next opens; <0 if none/already open
};

// What he owes, right now: every arc whose line he has written, whose goal is unmet, and
// whose `known_when` holds. Ordered as authored -- the file is the author's sense of the
// thread's weight, and a list that reshuffles itself as flags land is a list you cannot
// learn by position.
std::vector<Item> agenda(const Registry& reg, const unlock::Knowledge& k);

// When a window next opens, as a day-fraction, searching forward from `k.day_frac` within
// the day. Returns <0 if it is open now or does not open again today. Separate from agenda()
// so it is testable against a hand-built clause.
double nextOpening(const unlock::Condition& window, const unlock::Knowledge& k);

// Check the arc graph against what the world can produce. Reports:
//  - a route that can never be satisfied (requires an observation or flag nothing produces),
//  - an arc with fewer than two routes (a linear thread -- one path is a design bug here),
//  - a goal flag nothing reads (the arc completes and nothing responds),
//  - duplicate arc ids.
// Pure: returns the findings rather than printing, so it is testable. The loader prints.
std::vector<Problem> validate(const Registry& reg, const Producible& world);

} // namespace arcs
