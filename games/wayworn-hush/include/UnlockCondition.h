#pragma once

#include <nlohmann/json_fwd.hpp>

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// The unlock primitive -- the spine of the cognition engine. Every thing that can
// become newly available (a thought-roll, an action option, a re-lit signal, a
// conclusion, an encounter unlock) carries an `unlock_when`: a list of clauses,
// satisfied if ANY clause holds (OR of ANDs). Each clause is an AND of its set
// fields. A clause with no fields set is trivially true.
//
// This is pure logic: it reads a Knowledge snapshot and answers "is this now
// available?" -- no game/UI/GL coupling, fully testable. Authored in JSON:
//   unlock_when:
//     - { observed: [stone, water], stat: { perception: 3 } }   # clause A (AND)
//     - { flag: met_hermit }                                    # OR clause B
// See docs/design/PROCESSING-MODEL.md.
namespace unlock
{

// A read-only view of everything that gates availability, assembled by the caller
// from the observation + growth state. Passed to satisfaction checks so the
// primitive never depends on those concrete types.
struct Knowledge
{
    // Memory ids held -- observed encounters AND fired thoughts both live
    // here (a fired thought is a memory you can reference later). Reaching a
    // deeper objective tier is itself a memory ("<spot>@<tier>"), so tier-depth
    // gates flow through `observed` like everything else -- no separate concept.
    const std::unordered_set<std::string>* observed = nullptr;
    const std::unordered_set<std::string>* flags = nullptr;      // quest/event flags set
    const std::unordered_map<std::string, int>* stats = nullptr; // stat name -> level
    // Item ids in the satchel. Instruments gate CONTENT, never truth: the world
    // keeps its own time and a thought forms on its own, but reading the hour off
    // a note needs the watch and writing a thought down needs the notebook. That
    // asymmetry is authored as clauses, not compiled in.
    const std::unordered_set<std::string>* carrying = nullptr;

    bool has(const std::unordered_set<std::string>* set, const std::string& id) const;
    int stat(const std::string& name) const;
};

// One AND-clause. A field left empty is ignored. All set fields must hold.
// `observed` and `flags` are lists -- ALL listed entries must be held (this is
// how a synthesis thought requires a series of observations, or a deed waits on
// several flags at once).
struct Clause
{
    std::vector<std::string> observed;         // require ALL these memories observed
    std::vector<std::string> flags;            // require ALL these flags set
    std::unordered_map<std::string, int> stat; // require each stat >= its level
    std::vector<std::string> carrying;         // require ALL these items in the satchel
    // The NEGATIVE half: each entry must NOT be held. Same name space as the
    // positive fields ("flag:x", "obs:x", "item:x"), so one clause can say "he
    // has seen the thing but is not carrying the notebook" -- which is how a
    // thought aches instead of landing, and how someone notices empty hands.
    std::vector<std::string> without;
};

// A full unlock gate: OR of clauses. Empty (no clauses) = always available (an
// unconditional thing).
struct Condition
{
    std::vector<Clause> any;
};

// Does this clause hold under the given knowledge?
bool clauseHolds(const Clause& c, const Knowledge& k);

// Is the condition satisfied (any clause holds, or it is unconditional)?
bool satisfied(const Condition& cond, const Knowledge& k);

// Parse a Condition from a JSON array of clauses (each field string-or-array: `flag`,
// `observed` (all listed required), `stat` (name -> min level)). A non-array or missing -> an
// empty (unconditional) Condition. The ONE parser -- observations, crafting, and any future
// gated system share it so a clause is read identically everywhere.
Condition parseCondition(const nlohmann::json& j);

} // namespace unlock
