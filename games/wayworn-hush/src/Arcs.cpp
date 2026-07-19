#include "Arcs.h"

#include "JsonConfig.h"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <unordered_set>

namespace arcs
{

namespace
{
Arc parseArc(const nlohmann::json& j)
{
    Arc a;
    a.id = j.value("id", std::string{});
    a.goal_flag = j.value("goal_flag", std::string{});
    if (const auto it = j.find("routes"); it != j.end() && it->is_array())
        for (const auto& r : *it)
        {
            if (!r.is_object())
                continue;
            Route route;
            route.label = r.value("label", std::string{});
            if (const auto w = r.find("unlock_when"); w != r.end())
                route.when = unlock::parseCondition(*w);
            a.routes.push_back(std::move(route));
        }
    return a;
}

// Everything a clause demands the world be able to produce. A stat requirement is always
// satisfiable (stats grow), so only memories and flags are checked.
void clauseDemands(const unlock::Clause& c, std::vector<std::string>& observed,
                   std::vector<std::string>& flags)
{
    for (const auto& o : c.observed)
        observed.push_back(o);
    if (!c.flag.empty())
        flags.push_back(c.flag);
}
} // namespace

void load(Registry& out, const std::string& path)
{
    out.arcs.clear();
    const auto loaded = config::load(path);
    if (!loaded)
        return;
    const nlohmann::json& j = *loaded;
    const auto list = j.find("arcs");
    if (list == j.end() || !list->is_array())
        return;

    for (const auto& e : *list)
    {
        if (!e.is_object())
            continue;
        Arc a = parseArc(e);
        if (a.id.empty())
        {
            std::fprintf(stderr, "[arc] an arc has no id -- dropped\n");
            continue;
        }
        if (a.goal_flag.empty())
        {
            std::fprintf(stderr, "[arc] '%s' has no goal_flag -- dropped\n", a.id.c_str());
            continue;
        }
        out.arcs.push_back(std::move(a));
    }
}

namespace
{
// Every flag a condition gates on, into `out`.
void readsOf(const unlock::Condition& c, std::unordered_set<std::string>& out)
{
    for (const auto& clause : c.any)
        if (!clause.flag.empty())
            out.insert(clause.flag);
}
} // namespace

Producible survey(const observations::State& state)
{
    Producible w;
    for (const auto& e : state.encounters)
    {
        w.observable.insert(e.id);
        readsOf(e.visible_when, w.read_flags);
        // A tier reached is itself a memory ("<spot>@<n>"), the same id form gates use.
        for (std::size_t i = 0; i < e.tiers.size(); ++i)
        {
            w.observable.insert(e.id + "@" + std::to_string(i));
            readsOf(e.tiers[i].unlock_when, w.read_flags);
        }
        for (const auto& a : e.actions)
        {
            if (!a.set_flag.empty())
                w.flags.insert(a.set_flag);
            readsOf(a.unlock_when, w.read_flags);
        }
    }
    for (const auto& t : state.thoughts)
    {
        w.observable.insert(t.id); // a fired thought is a memory later content can require
        if (!t.set_flag.empty())
            w.flags.insert(t.set_flag);
        readsOf(t.unlock_when, w.read_flags);
    }
    return w;
}

namespace
{
// What a single clause demands that the world cannot produce. Empty = this clause is
// reachable, which is enough to make its whole route reachable (OR of ANDs).
std::vector<std::string> clauseGaps(const unlock::Clause& c, const Producible& world)
{
    std::vector<std::string> obs;
    std::vector<std::string> flg;
    clauseDemands(c, obs, flg);

    std::vector<std::string> gaps;
    for (const auto& o : obs)
        if (world.observable.count(o) == 0)
            gaps.push_back("nothing produces the memory '" + o + "'");
    for (const auto& f : flg)
        if (world.flags.count(f) == 0)
            gaps.push_back("nothing sets the flag '" + f + "'");
    return gaps;
}

// A route fails only when EVERY clause is blocked; then every gap found is reported so the
// author sees all of what is missing, not just the first.
void checkRoute(const std::string& arcId, const Route& r, const Producible& world,
                std::vector<Problem>& out)
{
    std::string who = "route '" + r.label + "'";
    if (r.label.empty())
        who = "a route";

    if (r.when.any.empty())
    {
        out.push_back({arcId, who + " is unconditional -- it opens the arc immediately"});
        return;
    }

    std::vector<std::string> missing;
    for (const auto& c : r.when.any)
    {
        std::vector<std::string> gaps = clauseGaps(c, world);
        if (gaps.empty())
            return; // this clause is reachable, so the route is
        for (auto& g : gaps)
            missing.push_back(std::move(g));
    }
    for (const auto& m : missing)
    {
        std::string detail = who;
        detail += " can never be satisfied: ";
        detail += m;
        out.push_back({arcId, detail});
    }
}

// The arc's own shape, independent of whether its routes can be walked.
void checkArcShape(const Arc& a, const Producible& world, std::unordered_set<std::string>& seen,
                   std::vector<Problem>& out)
{
    if (!seen.insert(a.id).second)
        out.push_back({a.id, "duplicate arc id"});

    // One route is a linear thread. An arc exists to offer several ways to the same
    // understanding; authoring only one is a design bug, not a style choice.
    if (a.routes.size() < 2)
        out.push_back({a.id, "has fewer than two routes -- the thread is linear"});

    if (world.read_flags.count(a.goal_flag) == 0)
    {
        std::string detail = "goal flag '";
        detail += a.goal_flag;
        detail += "' is never read -- the arc completes and nothing responds";
        out.push_back({a.id, detail});
    }
}
} // namespace

std::vector<Problem> validate(const Registry& reg, const Producible& world)
{
    std::vector<Problem> problems;
    std::unordered_set<std::string> seen;
    for (const auto& a : reg.arcs)
    {
        checkArcShape(a, world, seen, problems);
        for (const auto& r : a.routes)
            checkRoute(a.id, r, world, problems);
    }
    return problems;
}

} // namespace arcs
