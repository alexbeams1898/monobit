#include "log/Log.h"

#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace engine::log
{

namespace
{
// Registry of channels keyed by name. Channels live forever (boot to
// shutdown); we never destroy and re-create. Mutex guards lookup +
// insertion against multi-threaded init (worker threads logging
// during preload, etc.).
std::mutex& registry_mutex()
{
    static std::mutex m;
    return m;
}
std::unordered_map<std::string, std::unique_ptr<Channel>>& registry()
{
    static std::unordered_map<std::string, std::unique_ptr<Channel>> r;
    return r;
}

const char* level_tag(Level lv)
{
    switch (lv)
    {
    case Level::Trace:
        return "TRC";
    case Level::Debug:
        return "DBG";
    case Level::Info:
        return "INF";
    case Level::Warn:
        return "WRN";
    case Level::Error:
        return "ERR";
    }
    return "???";
}
} // namespace

Channel::Channel(std::string name, Sink sink, const char* file_path, Level min_level,
                 bool mirror_stderr)
    : channel_name(std::move(name)), sink_kind(sink), mirror_to_stderr(mirror_stderr),
      minimum_level(min_level)
{
    if (sink_kind == Sink::File)
    {
        // Truncate per run so each session's log is isolated. The
        // mirror_to_stderr flag is independent of file ownership.
        if (file_path != nullptr)
            file = std::fopen(file_path, "w");
    }
}

Channel::~Channel()
{
    if (file != nullptr)
    {
        std::fclose(file);
        file = nullptr;
    }
}

void Channel::write_formatted(Level lv, const std::string& formatted)
{
    // Format: "[channel:LV] message\n". The level tag is left small
    // so per-line overhead stays low for trace channels that emit
    // thousands of lines per session.
    std::FILE* primary = (sink_kind == Sink::Stderr) ? stderr : file;
    if (primary != nullptr)
    {
        std::fprintf(primary, "[%s:%s] %s\n", channel_name.c_str(), level_tag(lv),
                     formatted.c_str());
        // fflush every line — debug logs are read mid-crash; an
        // unflushed buffer loses the smoking gun. The per-line cost
        // is amortized by the line-buffered stderr redirect that
        // main.cpp installs at boot.
        std::fflush(primary);
    }
    if (sink_kind == Sink::File && mirror_to_stderr)
    {
        std::fprintf(stderr, "[%s:%s] %s\n", channel_name.c_str(), level_tag(lv),
                     formatted.c_str());
        std::fflush(stderr);
    }
}

Channel& Channel::get(std::string_view name, Sink sink, const char* file_path, Level min_level,
                      bool mirror_to_stderr)
{
    const std::lock_guard<std::mutex> guard(registry_mutex());
    auto& reg = registry();
    const std::string key(name);
    auto it = reg.find(key);
    if (it != reg.end())
        return *it->second;
    auto channel =
        std::unique_ptr<Channel>(new Channel(key, sink, file_path, min_level, mirror_to_stderr));
    auto* raw = channel.get();
    reg.emplace(key, std::move(channel));
    return *raw;
}

Channel* Channel::find(std::string_view name)
{
    const std::lock_guard<std::mutex> guard(registry_mutex());
    auto& reg = registry();
    const std::string key(name);
    auto it = reg.find(key);
    return it == reg.end() ? nullptr : it->second.get();
}

} // namespace engine::log
