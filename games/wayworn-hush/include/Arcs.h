#pragma once

#include "Observations.h"
#include "UnlockCondition.h"

#include <nlohmann/json_fwd.hpp>

#include <string>
#include <unordered_set>
#include <vector>

// Story arcs -- the authoring view of a thread. An arc is one goal flag reached by any of
// several ROUTES, so a looker, a reasoner and a maker can arrive at the same understanding by
// different means. Routes are `unlock::Condition`s, so an arc introduces no new gating concept:
// the world already holds the combinatorics (docs/design/GAME-SYSTEMS.md).
//
// This is authoring apparatus, NOT a runtime system. Nothing in the game loop consults an arc;
// play runs off flags exactly as it does without one. The arc file exists so a thread can be
// seen whole and CHECKED -- a renamed flag that quietly orphans a route is caught at load
// instead of in playtest. Delete arcs.json and the game plays identically.
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
    std::unordered_set<std::string> flags;      // flags some deed/thought can set
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
Producible survey(const observations::State& state);

// Check the arc graph against what the world can produce. Reports:
//  - a route that can never be satisfied (requires an observation or flag nothing produces),
//  - an arc with fewer than two routes (a linear thread -- one path is a design bug here),
//  - a goal flag nothing reads (the arc completes and nothing responds),
//  - duplicate arc ids.
// Pure: returns the findings rather than printing, so it is testable. The loader prints.
std::vector<Problem> validate(const Registry& reg, const Producible& world);

} // namespace arcs
