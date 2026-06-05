#include "dialog/TopicRegistry.h"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <fstream>
#include <unordered_set>

namespace selva::dialog
{

namespace
{

ShowWhen parseShowWhen(const nlohmann::json& j)
{
    ShowWhen sw;
    if (j.is_null() || !j.is_object())
        return sw;
    if (j.contains("flags_required") && j.at("flags_required").is_array())
    {
        for (const auto& f : j.at("flags_required"))
            if (f.is_string())
                sw.flags_required.push_back(f.get<std::string>());
    }
    if (j.contains("flags_forbidden") && j.at("flags_forbidden").is_array())
    {
        for (const auto& f : j.at("flags_forbidden"))
            if (f.is_string())
                sw.flags_forbidden.push_back(f.get<std::string>());
    }
    sw.custom = j.value("custom", std::string{});
    return sw;
}

bool isSentinelTopic(const std::string& s)
{
    return s == "__end__" || s == "__same__";
}

} // namespace

namespace
{
void parseChoices(const nlohmann::json& t, Topic& topic)
{
    if (!t.contains("choices") || !t.at("choices").is_array())
        return;
    for (const auto& c : t.at("choices"))
    {
        Choice ch;
        ch.id = c.value("id", std::string{});
        ch.label = c.value("label", std::string{});
        ch.next_topic = c.value("next_topic", std::string{});
        ch.choice_show_when = parseShowWhen(c.value("choice_show_when", nlohmann::json::object()));
        ch.on_select = c.value("on_select", std::string{});
        topic.choices.push_back(std::move(ch));
    }
}

bool parseTopic(const nlohmann::json& t, const std::string& npc_id,
                std::unordered_set<std::string>& topic_ids, Topic& out)
{
    out.id = t.value("id", std::string{});
    out.speaker = t.value("speaker", std::string{});
    out.line = t.value("line", std::string{});
    out.show_when = parseShowWhen(t.value("show_when", nlohmann::json::object()));
    out.on_enter = t.value("on_enter", std::string{});
    out.on_exit = t.value("on_exit", std::string{});
    out.entry_point = t.value("entry_point", false);
    if (out.id.empty())
    {
        std::fprintf(stderr, "[npc-dialog] '%s' topic missing id; skipping\n", npc_id.c_str());
        std::fflush(stderr);
        return false;
    }
    if (topic_ids.count(out.id) > 0)
    {
        std::fprintf(stderr, "[npc-dialog] '%s' duplicate topic id '%s'; skipping\n",
                     npc_id.c_str(), out.id.c_str());
        std::fflush(stderr);
        return false;
    }
    parseChoices(t, out);
    topic_ids.insert(out.id);
    return true;
}

void validateNextTopicReferences(const NpcDialog& npc,
                                 const std::unordered_set<std::string>& topic_ids)
{
    for (const auto& topic : npc.topics)
    {
        for (const auto& ch : topic.choices)
        {
            if (ch.next_topic.empty())
            {
                std::fprintf(stderr,
                             "[npc-dialog] '%s' topic '%s' choice '%s' has empty next_topic\n",
                             npc.npc_id.c_str(), topic.id.c_str(), ch.id.c_str());
                std::fflush(stderr);
                continue;
            }
            if (isSentinelTopic(ch.next_topic))
                continue;
            if (topic_ids.count(ch.next_topic) == 0)
            {
                std::fprintf(
                    stderr, "[npc-dialog] '%s' topic '%s' choice '%s' next_topic '%s' not found\n",
                    npc.npc_id.c_str(), topic.id.c_str(), ch.id.c_str(), ch.next_topic.c_str());
                std::fflush(stderr);
            }
        }
    }
}

bool loadOneNpcDialog(const std::filesystem::path& path, NpcDialog& out)
{
    std::ifstream in(path);
    if (!in)
    {
        std::fprintf(stderr, "[npc-dialog] cannot open %s\n", path.string().c_str());
        std::fflush(stderr);
        return false;
    }
    try
    {
        nlohmann::json j;
        in >> j;
        out.npc_id = j.value("npc_id", std::string{});
        out.display_name = j.value("display_name", std::string{});
        if (out.npc_id.empty())
        {
            std::fprintf(stderr, "[npc-dialog] %s missing npc_id; skipping\n",
                         path.string().c_str());
            std::fflush(stderr);
            return false;
        }
        if (!j.contains("topics") || !j.at("topics").is_array())
        {
            std::fprintf(stderr, "[npc-dialog] '%s' missing 'topics' array; skipping\n",
                         out.npc_id.c_str());
            std::fflush(stderr);
            return false;
        }
        std::unordered_set<std::string> topic_ids;
        for (const auto& t : j.at("topics"))
        {
            Topic topic;
            if (parseTopic(t, out.npc_id, topic_ids, topic))
                out.topics.push_back(std::move(topic));
        }
        validateNextTopicReferences(out, topic_ids);
        return true;
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "[npc-dialog] parse error in %s: %s\n", path.string().c_str(),
                     e.what());
        std::fflush(stderr);
        return false;
    }
}
} // namespace

void TopicRegistry::loadDirectory(const std::filesystem::path& dir)
{
    by_id.clear();
    if (!std::filesystem::exists(dir) || !std::filesystem::is_directory(dir))
    {
        std::fprintf(stderr, "[npc-dialog] directory not found: %s\n", dir.string().c_str());
        std::fflush(stderr);
        return;
    }
    for (const auto& entry : std::filesystem::directory_iterator(dir))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".json")
            continue;
        NpcDialog npc;
        if (!loadOneNpcDialog(entry.path(), npc))
            continue;
        const std::string id = npc.npc_id;
        by_id[id] = std::move(npc);
        std::fprintf(stderr, "[npc-dialog] loaded '%s' (%zu topics) from %s\n", id.c_str(),
                     by_id[id].topics.size(), entry.path().string().c_str());
        std::fflush(stderr);
    }
}

const NpcDialog* TopicRegistry::get(const std::string& npc_id) const
{
    const auto it = by_id.find(npc_id);
    return (it == by_id.end()) ? nullptr : &it->second;
}

const Topic* TopicRegistry::getTopic(const std::string& npc_id, const std::string& topic_id) const
{
    const NpcDialog* npc = get(npc_id);
    if (npc == nullptr)
        return nullptr;
    for (const auto& t : npc->topics)
    {
        if (t.id == topic_id)
            return &t;
    }
    return nullptr;
}

TopicRegistry& topicRegistry()
{
    static TopicRegistry instance;
    return instance;
}

} // namespace selva::dialog
