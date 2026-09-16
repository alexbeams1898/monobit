#include "insight/Insight.h"

#include "AppStateGlobal.h"
#include "insight/InsightLayout.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace selva::insight
{

namespace
{

enum class TriggerKind : std::uint8_t
{
    FlagSet,
    DialogBegan,
    Examined,
    KillCount,
    SangueAccumulated,
};

// Per cognition-system.md, inference nodes carry an authored `readings`
// array. Each reading is one of 3-5
// interpretations the player can pick at Deduce time. A reading
// declares its `warrant_evidence` set: the specific observation ids
// the player must have linked for that reading to count as
// warranted. The reading's text is the Vagrant's interior voice at
// that interpretation. Observations have no readings.
struct Reading
{
    std::string id;
    std::string text;
};

// One node = one trigger (observations) OR one inference (with
// readings). Triggers describe how an observation fires; inferences
// don't have triggers -- they fire only when the player deduces them.
struct Node
{
    std::string node_id;
    NodeKind node_kind = NodeKind::Observation;
    TriggerKind kind = TriggerKind::FlagSet;
    std::string string_arg;           // flag name / npc id / mesh name / archetype id
    std::uint32_t threshold_arg = 0u; // kill_count threshold / sangue threshold
    Category category = Category::Unknown;
    // Inference-only: observation ids the player must have selected
    // together at the moment of Deduce. Empty for observations.
    std::vector<std::string> requires_ids;
    // Inference-only: authored readings (3-5 typical). At Deduce time
    // the player picks one; the picked reading's warrant_evidence set
    // is what determines whether the inference is warranted.
    std::vector<Reading> readings;
    // Observations that promote this inference from uncertain to certain.
    // Read by tryConfirm and confirmedByOf; empty means there is nothing
    // to confirm and the inference fires certain on the spot.
    std::vector<std::string> confirmed_by_ids;
    // Authored canvas position. Unused by the workbench (player-set
    // positions live on PlayerProfile) but kept for any future
    // auto-layout hint surface.
    NodePos pos;
};

std::vector<Node>& sNodes()
{
    static std::vector<Node> v;
    return v;
}

bool parseTriggerKind(const std::string& s, TriggerKind& out)
{
    if (s == "flag_set")
    {
        out = TriggerKind::FlagSet;
        return true;
    }
    if (s == "dialog_began")
    {
        out = TriggerKind::DialogBegan;
        return true;
    }
    if (s == "examined")
    {
        out = TriggerKind::Examined;
        return true;
    }
    if (s == "kill_count")
    {
        out = TriggerKind::KillCount;
        return true;
    }
    if (s == "sangue_accumulated")
    {
        out = TriggerKind::SangueAccumulated;
        return true;
    }
    return false;
}

// Convention for synthesized flag names used by event hooks. These
// land in PlayerProfile.flags via setFlag(); the FlagSet trigger
// kind reads them. Keeping the namespace ("examined:", "talked:")
// distinct from gameplay flags ("lupa_felled") prevents collisions.
std::string examinedFlag(const std::string& mesh_debug_name)
{
    return "examined:" + mesh_debug_name;
}
std::string talkedFlag(const std::string& npc_id)
{
    return "talked:" + npc_id;
}

// Open + parse a json file from disk. Returns the loaded object or
// std::nullopt on failure (with stderr diagnostic).
std::optional<nlohmann::json> openInsightJson(const std::filesystem::path& path)
{
    std::ifstream in(path);
    if (!in.is_open())
    {
        std::fprintf(stderr, "[insight] cannot open '%s'\n", path.string().c_str());
        return std::nullopt;
    }
    nlohmann::json j;
    try
    {
        in >> j;
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "[insight] '%s' parse error: %s\n", path.string().c_str(), e.what());
        return std::nullopt;
    }
    if (!j.is_object())
    {
        std::fprintf(stderr, "[insight] '%s' top-level must be an object\n", path.string().c_str());
        return std::nullopt;
    }
    return j;
}

// Parse the "kind" field if present; mutates n.node_kind on success.
void parseNodeKindField(const nlohmann::json& v, Node& n, const std::filesystem::path& path,
                        const std::string& node_id)
{
    if (!v.contains("kind") || !v["kind"].is_string())
        return;
    const std::string nk = v["kind"].get<std::string>();
    if (nk == "observation")
        n.node_kind = NodeKind::Observation;
    else if (nk == "inference" || nk == "conclusion") // 'conclusion' is back-compat
        n.node_kind = NodeKind::Inference;
    else
        std::fprintf(stderr,
                     "[insight] '%s' node '%s' unknown kind '%s' (expected "
                     "observation/inference); defaulting to observation\n",
                     path.string().c_str(), node_id.c_str(), nk.c_str());
}

// Parse the inference's `requires` array. Returns false if missing/
// malformed, in which case the node should be skipped.
bool parseInferenceRequires(const nlohmann::json& v, Node& n, const std::filesystem::path& path,
                            const std::string& node_id)
{
    if (!v.contains("requires") || !v["requires"].is_array() || v["requires"].empty())
    {
        std::fprintf(stderr,
                     "[insight] '%s' node '%s' conclusion missing non-empty 'requires' array\n",
                     path.string().c_str(), node_id.c_str());
        return false;
    }
    for (const auto& r : v["requires"])
    {
        if (!r.is_string())
        {
            std::fprintf(stderr,
                         "[insight] '%s' node '%s' conclusion 'requires' entry not a string\n",
                         path.string().c_str(), node_id.c_str());
            n.requires_ids.clear();
            return false;
        }
        n.requires_ids.push_back(r.get<std::string>());
    }
    return !n.requires_ids.empty();
}

// Parse the inference's optional `readings` array.
void parseInferenceReadings(const nlohmann::json& v, Node& n, const std::filesystem::path& path,
                            const std::string& node_id)
{
    if (!v.contains("readings") || !v["readings"].is_array())
        return;
    for (const auto& r : v["readings"])
    {
        if (!r.is_object() || !r.contains("id") || !r["id"].is_string())
        {
            std::fprintf(stderr, "[insight] '%s' node '%s' reading missing 'id' string\n",
                         path.string().c_str(), node_id.c_str());
            continue;
        }
        Reading rd;
        rd.id = r["id"].get<std::string>();
        if (r.contains("text") && r["text"].is_string())
            rd.text = r["text"].get<std::string>();
        n.readings.push_back(std::move(rd));
    }
}

// Parse the inference's deprecated `confirmed_by` list with a warning.
void parseInferenceConfirmedBy(const nlohmann::json& v, Node& n, const std::filesystem::path& path,
                               const std::string& node_id)
{
    if (!v.contains("confirmed_by") || !v["confirmed_by"].is_array())
        return;
    std::fprintf(stderr,
                 "[insight] '%s' node '%s' uses deprecated 'confirmed_by' (cognition-"
                 "system v1 replaced this with `readings`)\n",
                 path.string().c_str(), node_id.c_str());
    for (const auto& r : v["confirmed_by"])
        if (r.is_string())
            n.confirmed_by_ids.push_back(r.get<std::string>());
}

// Parse the per-trigger-kind required fields. Returns false on schema
// mismatch (caller should skip the node).
// Per-kind argument parsers. Each writes into n on success; logs +
// returns false on schema mismatch.
bool parseFlagSetArgs(const nlohmann::json& t, Node& n, const std::filesystem::path& path,
                      const std::string& node_id)
{
    if (!t.contains("flag") || !t["flag"].is_string())
    {
        std::fprintf(stderr, "[insight] '%s' node '%s' flag_set trigger missing 'flag' string\n",
                     path.string().c_str(), node_id.c_str());
        return false;
    }
    n.string_arg = t["flag"].get<std::string>();
    return true;
}
bool parseDialogBeganArgs(const nlohmann::json& t, Node& n, const std::filesystem::path& path,
                          const std::string& node_id)
{
    if (!t.contains("npc_id") || !t["npc_id"].is_string())
    {
        std::fprintf(stderr, "[insight] '%s' node '%s' dialog_began trigger missing 'npc_id'\n",
                     path.string().c_str(), node_id.c_str());
        return false;
    }
    n.string_arg = t["npc_id"].get<std::string>();
    return true;
}
bool parseExaminedArgs(const nlohmann::json& t, Node& n, const std::filesystem::path& path,
                       const std::string& node_id)
{
    if (t.contains("subject") && t["subject"].is_string())
        n.string_arg = t["subject"].get<std::string>();
    else if (t.contains("mesh_debug_name") && t["mesh_debug_name"].is_string())
        n.string_arg = t["mesh_debug_name"].get<std::string>();
    else
    {
        std::fprintf(stderr, "[insight] '%s' node '%s' examined trigger missing 'subject' string\n",
                     path.string().c_str(), node_id.c_str());
        return false;
    }
    return true;
}
bool parseKillCountArgs(const nlohmann::json& t, Node& n, const std::filesystem::path& path,
                        const std::string& node_id)
{
    if (!t.contains("archetype") || !t["archetype"].is_string() || !t.contains("threshold") ||
        !t["threshold"].is_number_unsigned())
    {
        std::fprintf(stderr,
                     "[insight] '%s' node '%s' kill_count trigger missing "
                     "'archetype'/'threshold'\n",
                     path.string().c_str(), node_id.c_str());
        return false;
    }
    n.string_arg = t["archetype"].get<std::string>();
    n.threshold_arg = t["threshold"].get<std::uint32_t>();
    return true;
}
bool parseSangueAccumulatedArgs(const nlohmann::json& t, Node& n, const std::filesystem::path& path,
                                const std::string& node_id)
{
    if (!t.contains("threshold") || !t["threshold"].is_number_unsigned())
    {
        std::fprintf(stderr,
                     "[insight] '%s' node '%s' sangue_accumulated trigger missing 'threshold'\n",
                     path.string().c_str(), node_id.c_str());
        return false;
    }
    n.threshold_arg = t["threshold"].get<std::uint32_t>();
    return true;
}

bool parseTriggerArgs(const nlohmann::json& t, Node& n, const std::filesystem::path& path,
                      const std::string& node_id)
{
    switch (n.kind)
    {
    case TriggerKind::FlagSet:
        return parseFlagSetArgs(t, n, path, node_id);
    case TriggerKind::DialogBegan:
        return parseDialogBeganArgs(t, n, path, node_id);
    case TriggerKind::Examined:
        return parseExaminedArgs(t, n, path, node_id);
    case TriggerKind::KillCount:
        return parseKillCountArgs(t, n, path, node_id);
    case TriggerKind::SangueAccumulated:
        return parseSangueAccumulatedArgs(t, n, path, node_id);
    }
    return false;
}

// Parse the observation's trigger sub-object. Returns false if absent
// or invalid (caller should skip the node).
bool parseObservationTrigger(const nlohmann::json& v, Node& n, const std::filesystem::path& path,
                             const std::string& node_id)
{
    if (!v.contains("trigger") || !v["trigger"].is_object())
    {
        std::fprintf(stderr, "[insight] '%s' node '%s' missing 'trigger' object\n",
                     path.string().c_str(), node_id.c_str());
        return false;
    }
    const auto& t = v["trigger"];
    if (!t.contains("kind") || !t["kind"].is_string())
    {
        std::fprintf(stderr, "[insight] '%s' node '%s' trigger missing 'kind'\n",
                     path.string().c_str(), node_id.c_str());
        return false;
    }
    if (!parseTriggerKind(t["kind"].get<std::string>(), n.kind))
    {
        std::fprintf(stderr, "[insight] '%s' node '%s' unknown trigger kind '%s'\n",
                     path.string().c_str(), node_id.c_str(), t["kind"].get<std::string>().c_str());
        return false;
    }
    return parseTriggerArgs(t, n, path, node_id);
}

// Parse optional `category` field (Mind sub-page grouping). Defaults
// to Unknown (logged) when absent.
void parseCategoryField(const nlohmann::json& v, Node& n, const std::filesystem::path& path,
                        const std::string& node_id)
{
    if (!v.contains("category") || !v["category"].is_string())
    {
        std::fprintf(stderr,
                     "[insight] '%s' node '%s' missing 'category' field; will not appear on "
                     "Mind sub-page\n",
                     path.string().c_str(), node_id.c_str());
        return;
    }
    const std::string cat = v["category"].get<std::string>();
    if (cat == "world")
        n.category = Category::World;
    else if (cat == "self")
        n.category = Category::Self;
    else if (cat == "others")
        n.category = Category::Others;
    else
        std::fprintf(stderr,
                     "[insight] '%s' node '%s' unknown category '%s' (expected "
                     "world/self/others); falling back to Unknown\n",
                     path.string().c_str(), node_id.c_str(), cat.c_str());
}

// Parse optional `pos` field (Mind sub-page canvas position).
void parsePosField(const nlohmann::json& v, Node& n)
{
    if (!v.contains("pos") || !v["pos"].is_object())
        return;
    const auto& pos = v["pos"];
    if (pos.contains("x") && pos["x"].is_number())
        n.pos.x = pos["x"].get<float>();
    if (pos.contains("y") && pos["y"].is_number())
        n.pos.y = pos["y"].get<float>();
}

// Build a single Node from one JSON object entry. Returns false when
// the entry is malformed (caller skips).
bool buildNodeFromJson(const std::string& node_id, const nlohmann::json& v, Node& n,
                       const std::filesystem::path& path)
{
    n.node_id = node_id;
    parseNodeKindField(v, n, path, node_id);
    if (n.node_kind == NodeKind::Inference)
    {
        if (!parseInferenceRequires(v, n, path, node_id))
            return false;
        parseInferenceReadings(v, n, path, node_id);
        parseInferenceConfirmedBy(v, n, path, node_id);
    }
    else
    {
        if (!parseObservationTrigger(v, n, path, node_id))
            return false;
    }
    parseCategoryField(v, n, path, node_id);
    parsePosField(v, n);
    return true;
}

bool loadOneFile(const std::filesystem::path& path)
{
    auto maybe_j = openInsightJson(path);
    if (!maybe_j.has_value())
        return false;
    const nlohmann::json& j = *maybe_j;
    int loaded = 0;
    for (auto it = j.begin(); it != j.end(); ++it)
    {
        const std::string& node_id = it.key();
        if (node_id.empty() || node_id[0] == '_')
            continue;
        const auto& v = it.value();
        if (!v.is_object())
        {
            std::fprintf(stderr, "[insight] '%s' node '%s' must be an object\n",
                         path.string().c_str(), node_id.c_str());
            continue;
        }
        Node n;
        if (!buildNodeFromJson(node_id, v, n, path))
            continue;
        sNodes().push_back(std::move(n));
        ++loaded;
    }
    std::fprintf(stderr, "[insight] loaded %d nodes from %s\n", loaded, path.string().c_str());
    return true;
}

// True when every id in confirmed_by_ids is currently unlocked on
// the profile. Vacuously true for empty lists (callers handle that
// separately -- a conclusion with no confirmed_by has no uncertain
// state to promote out of).
bool allConfirmedByUnlocked(const Node& n, const PlayerProfile* p)
{
    if (p == nullptr)
        return false;
    for (const auto& obs : n.confirmed_by_ids)
    {
        if (!selva::hasInsight(p, obs))
            return false;
    }
    return true;
}

// Promote a conclusion to certain if it isn't already. Idempotent.
// Caller has already confirmed the node is a conclusion + currently
// unlocked. Logs the promotion for visibility.
void promoteToCertain(PlayerProfile* p, const std::string& node_id)
{
    if (p == nullptr)
        return;
    if (std::find(p->certain_conclusions.begin(), p->certain_conclusions.end(), node_id) !=
        p->certain_conclusions.end())
        return;
    p->certain_conclusions.push_back(node_id);
    std::fprintf(stderr, "[insight] conclusion promoted to certain: '%s'\n", node_id.c_str());
    std::fflush(stderr);
}

bool evaluate(const Node& n, const PlayerProfile* p)
{
    if (p == nullptr)
        return false;
    switch (n.kind)
    {
    case TriggerKind::FlagSet:
        return selva::hasFlag(p, n.string_arg);
    case TriggerKind::DialogBegan:
        return selva::hasFlag(p, talkedFlag(n.string_arg));
    case TriggerKind::Examined:
        return selva::hasFlag(p, examinedFlag(n.string_arg));
    case TriggerKind::KillCount:
    {
        auto it = p->kill_counts.find(n.string_arg);
        if (it == p->kill_counts.end())
            return false;
        return it->second >= n.threshold_arg;
    }
    case TriggerKind::SangueAccumulated:
        return p->sangue_lifetime >= n.threshold_arg;
    }
    return false;
}

} // namespace

void loadDirectory(const std::string& dir_path)
{
    sNodes().clear();
    std::error_code ec;
    if (!std::filesystem::exists(dir_path, ec) || !std::filesystem::is_directory(dir_path, ec))
    {
        std::fprintf(stderr, "[insight] directory '%s' does not exist; no nodes loaded\n",
                     dir_path.c_str());
        return;
    }
    for (const auto& entry : std::filesystem::directory_iterator(dir_path, ec))
    {
        if (!entry.is_regular_file())
            continue;
        if (entry.path().extension() != ".json")
            continue;
        loadOneFile(entry.path());
    }
    std::fprintf(stderr, "[insight] total nodes loaded: %zu\n", sNodes().size());
}

void tick()
{
    PlayerProfile* p = selva::activePlayerProfile();
    if (p == nullptr)
        return;
    bool any_change = false;
    for (const auto& n : sNodes())
    {
        // Conclusions never auto-fire; they're player-driven via
        // deduction. The per-frame walk is observation-only.
        if (n.node_kind == NodeKind::Inference)
            continue;
        if (selva::hasInsight(p, n.node_id))
            continue;
        if (evaluate(n, p))
        {
            selva::setInsight(p, n.node_id);
            std::fprintf(stderr, "[insight] node fired: '%s'\n", n.node_id.c_str());
            std::fflush(stderr);
            // A new observation grows Perception.
            selva::growPerception();
            any_change = true;
        }
    }
    // Certainty is explicitly player-driven via tryConfirm(). tick()
    // does NOT auto-promote uncertain conclusions even when their
    // confirmed_by set is satisfied -- the player has to do the act
    // of linking the testimony to the conclusion on the Mind page.
    // Re-layout once per tick when anything changed (cheap at this node
    // count; skip when nothing fired to avoid recomputing every frame).
    if (any_change)
        selva::insight::layout::recompute();
}

void notifyDialogBegan(const std::string& npc_id)
{
    if (npc_id.empty())
        return;
    // Bookkeeping flag the DialogBegan trigger reads. Use the flag
    // helper so it persists in the profile (across save/load) without
    // a separate storage path.
    selva::setFlag(talkedFlag(npc_id));
}

void notifyExamined(const std::string& mesh_debug_name)
{
    if (mesh_debug_name.empty())
        return;
    selva::setFlag(examinedFlag(mesh_debug_name));
    // Per-subject examine count + Nth-time flag, so insight nodes can
    // gate on "examined twice" / "examined three times" etc. The
    // first-time flag above stays as the existing tier-0 gate. Count
    // is post-increment, so the FIRST call lands count=1 and sets
    // examined:<subject>:1; the second call lands count=2 and sets
    // examined:<subject>:2; etc.
    PlayerProfile* p = selva::activePlayerProfile();
    if (p == nullptr)
        return;
    const std::uint32_t new_count = ++p->examine_counts[mesh_debug_name];
    char buf[24];
    std::snprintf(buf, sizeof(buf), ":%u", new_count);
    selva::setFlag(examinedFlag(mesh_debug_name) + buf);
}

std::uint32_t examineCountOf(const std::string& subject)
{
    const PlayerProfile* p = selva::activePlayerProfile();
    if (p == nullptr)
        return 0u;
    auto it = p->examine_counts.find(subject);
    return (it == p->examine_counts.end()) ? 0u : it->second;
}

void notifyKill(const std::string& archetype_id)
{
    if (archetype_id.empty())
        return;
    PlayerProfile* p = selva::activePlayerProfile();
    if (p == nullptr)
        return;
    p->kill_counts[archetype_id] += 1u;
}

void resetGraph()
{
    sNodes().clear();
}

Category categoryOf(const std::string& node_id)
{
    for (const auto& n : sNodes())
    {
        if (n.node_id == node_id)
            return n.category;
    }
    return Category::Unknown;
}

bool tryConfirm(const std::string& conclusion_id, const std::string& observation_id)
{
    PlayerProfile* p = selva::activePlayerProfile();
    if (p == nullptr || conclusion_id.empty() || observation_id.empty())
        return false;
    if (!selva::hasInsight(p, conclusion_id) || !selva::hasInsight(p, observation_id))
        return false;
    // Find the conclusion node + validate.
    for (const auto& n : sNodes())
    {
        if (n.node_id != conclusion_id)
            continue;
        if (n.node_kind != NodeKind::Inference)
            return false;
        if (n.confirmed_by_ids.empty())
            return false; // not a confirmable conclusion
        if (std::find(n.confirmed_by_ids.begin(), n.confirmed_by_ids.end(), observation_id) ==
            n.confirmed_by_ids.end())
            return false; // observation isn't authored as a confirming source
        if (std::find(p->certain_conclusions.begin(), p->certain_conclusions.end(),
                      conclusion_id) != p->certain_conclusions.end())
            return false; // already certain
        promoteToCertain(p, conclusion_id);
        selva::insight::layout::recompute();
        return true;
    }
    return false;
}

NodeKind kindOf(const std::string& node_id)
{
    for (const auto& n : sNodes())
    {
        if (n.node_id == node_id)
            return n.node_kind;
    }
    return NodeKind::Observation;
}

std::string sourceOf(const std::string& node_id)
{
    for (const auto& n : sNodes())
    {
        if (n.node_id != node_id)
            continue;
        if (n.node_kind == NodeKind::Inference)
            return {};
        switch (n.kind)
        {
        case TriggerKind::Examined:
        case TriggerKind::DialogBegan:
        case TriggerKind::KillCount:
            return n.string_arg;
        case TriggerKind::FlagSet:
        {
            // Recognize the synthetic count-flag convention set by
            // notifyExamined ("examined:<subject>" for first-time +
            // "examined:<subject>:<N>" for Nth-time). Both flavors
            // share the underlying examine subject, which we extract
            // as the source so multi-tier examines cluster correctly.
            const std::string& f = n.string_arg;
            constexpr std::string_view kPrefix = "examined:";
            if (f.compare(0, kPrefix.size(), kPrefix.data(), kPrefix.size()) == 0)
            {
                const std::size_t colon = f.find(':', kPrefix.size());
                if (colon == std::string::npos)
                    return f.substr(kPrefix.size());                     // "examined:<subject>"
                return f.substr(kPrefix.size(), colon - kPrefix.size()); // "examined:<subject>:<N>"
            }
            return {};
        }
        case TriggerKind::SangueAccumulated:
        default:
            return {};
        }
    }
    return {};
}

NodePos posOf(const std::string& node_id)
{
    for (const auto& n : sNodes())
    {
        if (n.node_id == node_id)
            return n.pos;
    }
    return {};
}

std::vector<std::string> requiresOf(const std::string& node_id)
{
    for (const auto& n : sNodes())
    {
        if (n.node_id == node_id)
            return n.requires_ids;
    }
    return {};
}

std::vector<std::string> confirmedByOf(const std::string& node_id)
{
    for (const auto& n : sNodes())
    {
        if (n.node_id == node_id)
            return n.confirmed_by_ids;
    }
    return {};
}

std::vector<ReadingSpec> readingsOf(const std::string& node_id)
{
    for (const auto& n : sNodes())
    {
        if (n.node_id != node_id)
            continue;
        std::vector<ReadingSpec> out;
        out.reserve(n.readings.size());
        for (const auto& r : n.readings)
        {
            ReadingSpec s;
            s.id = r.id;
            s.text = r.text;
            out.push_back(std::move(s));
        }
        return out;
    }
    return {};
}

bool isCertain(const std::string& node_id)
{
    const PlayerProfile* p = selva::activePlayerProfile();
    if (p == nullptr)
        return false;
    return std::find(p->certain_conclusions.begin(), p->certain_conclusions.end(), node_id) !=
           p->certain_conclusions.end();
}

std::vector<std::string> firedInsights()
{
    std::vector<std::string> out;
    const PlayerProfile* p = selva::activePlayerProfile();
    if (p == nullptr)
        return out;
    for (const auto& n : sNodes())
    {
        if (std::find(p->unlocked_insights.begin(), p->unlocked_insights.end(), n.node_id) !=
            p->unlocked_insights.end())
        {
            out.push_back(n.node_id);
        }
    }
    return out;
}

namespace
{
bool sameStringSet(const std::vector<std::string>& a, const std::vector<std::string>& b)
{
    if (a.size() != b.size())
        return false;
    for (const auto& x : a)
        if (std::find(b.begin(), b.end(), x) == b.end())
            return false;
    return true;
}
} // namespace

std::string matchInference(const std::vector<std::string>& selected_observation_ids)
{
    const PlayerProfile* p = selva::activePlayerProfile();
    if (p == nullptr || selected_observation_ids.empty())
        return {};
    // Caller must have unlocked every observation in the selection.
    for (const auto& obs : selected_observation_ids)
        if (!selva::hasInsight(p, obs))
            return {};
    // Match if the inference's `requires` is a SUBSET of the
    // selection. The selection itself becomes linked_observations
    // on the resulting WorkbenchNode. Picking a smaller selection
    // matches a "basic" inference; adding evidence to the selection
    // can match more specific inferences AND warrant deeper readings.
    auto subsetOf =
        [](const std::vector<std::string>& subset, const std::vector<std::string>& superset)
    {
        for (const auto& x : subset)
            if (std::find(superset.begin(), superset.end(), x) == superset.end())
                return false;
        return true;
    };
    for (const auto& n : sNodes())
    {
        if (n.node_kind != NodeKind::Inference)
            continue;
        if (subsetOf(n.requires_ids, selected_observation_ids))
            return n.node_id;
    }
    return {};
}

std::string commitDeduce(const std::string& inference_id)
{
    PlayerProfile* p = selva::activePlayerProfile();
    if (p == nullptr || inference_id.empty())
        return {};
    for (const auto& n : sNodes())
    {
        if (n.node_id != inference_id)
            continue;
        if (n.node_kind != NodeKind::Inference)
            return {};
        if (selva::hasInsight(p, n.node_id))
        {
            // Already unlocked. Workbench can re-place; this call
            // is a no-op cosmologically. Return the id so the caller
            // knows it's "valid to place."
            return n.node_id;
        }
        selva::setInsight(p, n.node_id);
        std::fprintf(stderr, "[insight] inference fired: '%s'\n", n.node_id.c_str());
        std::fflush(stderr);
        // A new inference grows Cognition. The
        // Intelligence bump happens after the UI confirms the reading
        // pick is warranted -- the UI calls growIntelligence() directly
        // because warrantedness depends on the chosen reading.
        selva::growCognition();
        selva::insight::layout::recompute();
        return n.node_id;
    }
    return {};
}

} // namespace selva::insight
