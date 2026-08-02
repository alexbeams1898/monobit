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
    a.line = j.value("line", std::string{});
    if (const auto w = j.find("known_when"); w != j.end())
        a.known_when = unlock::parseCondition(*w);
    // A window is authored as the clause it is -- {"between": [...], "day_min": n} -- so it
    // reads the same as every other gate. Wrapped into a one-clause condition here.
    if (const auto w = j.find("window"); w != j.end())
    {
        if (w->is_array())
            a.window = unlock::parseCondition(*w);
        else if (w->is_object())
            a.window = unlock::parseCondition(nlohmann::json::array({*w}));
    }
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
    for (const auto& f : c.flags)
        flags.push_back(f);
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
        for (const auto& f : clause.flags)
            out.insert(f);
}
} // namespace

Producible survey(const psyche::State& state)
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
// Probe the window forward through the rest of the day, asking unlock:: the same question the
// world asks. Stepping a copy of the Knowledge (rather than reading `from`/`to` off the
// clauses) keeps the meaning of a window in ONE place -- an hour that reads open here is an
// hour the gate itself would open, whatever fields a future clause grows.
constexpr int kProbesPerDay = 24 * 4; // quarter-hour resolution: finer than any authored time
} // namespace

double nextOpening(const unlock::Condition& window, const unlock::Knowledge& k)
{
    if (window.any.empty() || unlock::satisfied(window, k))
        return -1.0;
    unlock::Knowledge probe = k;
    const double step = 1.0 / kProbesPerDay;
    for (int i = 1; i <= kProbesPerDay; ++i)
    {
        const double at = k.day_frac + step * i;
        if (at >= 1.0)
            break; // tomorrow is a different day, and a day_min clause would answer differently
        probe.day_frac = at;
        if (unlock::satisfied(window, probe))
            return at;
    }
    return -1.0;
}

std::vector<Item> agenda(const Registry& reg, const unlock::Knowledge& k)
{
    std::vector<Item> out;
    for (const auto& a : reg.arcs)
    {
        // No line = scaffolding, not an errand. Done = off the list; what came of it is
        // already written in the thoughts it produced.
        if (a.line.empty() || k.has(k.flags, a.goal_flag) || !unlock::satisfied(a.known_when, k))
            continue;

        Item item;
        item.arc = &a;
        if (unlock::satisfied(a.window, k))
            item.openness = Openness::Open;
        else if (const double at = nextOpening(a.window, k); at >= 0.0)
        {
            item.openness = Openness::ShutUntil;
            item.opens_at = at;
        }
        else
            item.openness = Openness::ShutToday;
        out.push_back(item);
    }
    return out;
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
    // understanding; authoring only one is a design bug, not a style choice -- EXCEPT for a
    // thread he has written down, which is an errand someone asked of him and may honestly
    // have one way to discharge it.
    if (a.routes.size() < 2 && a.line.empty())
        out.push_back({a.id, "has fewer than two routes -- the thread is linear"});

    // An errand nobody can learn of is an errand that never reaches the agenda.
    if (!a.line.empty() && !a.known_when.any.empty())
    {
        bool learnable = false;
        for (const auto& c : a.known_when.any)
            if (clauseGaps(c, world).empty())
                learnable = true;
        if (!learnable)
            out.push_back({a.id, "is written down but nothing can make it known"});
    }

    if (world.read_flags.count(a.goal_flag) == 0)
    {
        std::string detail = "goal flag '";
        detail += a.goal_flag;
        detail += "' is never read -- the arc completes and nothing responds";
        out.push_back({a.id, detail});
    }

    // The other half, and the worse bug: a thread nothing can finish. A goal no deed, thought
    // or clearing raises is an errand the pilgrim carries for the whole walk.
    if (world.flags.count(a.goal_flag) == 0)
    {
        std::string detail = "goal flag '";
        detail += a.goal_flag;
        detail += "' is never set -- nothing in the world can finish this thread";
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
