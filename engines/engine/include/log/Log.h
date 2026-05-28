#pragma once

#include <cstdio>
#include <string>
#include <string_view>

#include <fmt/core.h>

namespace engine::log
{

// Severity level. Channels gate by minimum level — messages below the
// channel's threshold are skipped at the call site with zero cost
// (the variadic pack is never formatted).
enum class Level
{
    Trace = 0, // fine-grained per-frame instrumentation
    Debug = 1, // diagnostic detail, off in shipping builds
    Info = 2,  // boot lifecycle, scene transitions, init success
    Warn = 3,  // recoverable problem
    Error = 4, // unrecoverable problem
};

// Where a channel's output goes. A channel can have ONE primary sink
// (file or stderr). For dual-sink ("mirror to both"), set
// mirror_to_stderr=true on a File channel.
enum class Sink
{
    File,   // owns a FILE* opened via std::fopen
    Stderr, // writes to stderr directly (no file open)
};

// One named log channel. Each module owns one. Construct at file
// scope; the registry interns channels by name so the same name
// resolves to the same instance across translation units.
class Channel
{
  public:
    // Look up or create a channel by name. The first caller's config
    // (file_path, sink, level, mirror_to_stderr) wins; subsequent
    // get() calls with the same name return the existing instance
    // and ignore the config args. Pass file_path=nullptr for Stderr.
    static Channel& get(std::string_view name, Sink sink, const char* file_path = nullptr,
                        Level min_level = Level::Trace, bool mirror_to_stderr = false);

    // Lookup-only accessor: returns the existing channel by name or
    // nullptr if not yet registered. Use when a downstream consumer
    // wants to write through a channel that some OTHER module owns
    // (e.g. PoseSampler writing through the combat-owned channel).
    // If the channel hasn't been registered yet, writes are silently
    // dropped — which is the right behavior for a diagnostic channel
    // that's only meaningful when its owner has enabled it.
    static Channel* find(std::string_view name);

    Channel(const Channel&) = delete;
    Channel& operator=(const Channel&) = delete;

    // The write entry point. Compile-time format-string check via
    // fmt::format_string<Args...>; runtime is gated on enabled +
    // min_level, so disabled channels pay nothing.
    template <typename... Args>
    void write(Level lv, fmt::format_string<Args...> fmt, Args&&... args)
    {
        if (!is_enabled || static_cast<int>(lv) < static_cast<int>(minimum_level))
            return;
        write_formatted(lv, fmt::format(fmt, std::forward<Args>(args)...));
    }

    // Convenience helpers — same gate, same format. Call site reads
    // naturally (channel.info("..."), channel.error("..."), etc).
    template <typename... Args> void trace(fmt::format_string<Args...> fmt, Args&&... args)
    {
        write(Level::Trace, fmt, std::forward<Args>(args)...);
    }
    template <typename... Args> void debug(fmt::format_string<Args...> fmt, Args&&... args)
    {
        write(Level::Debug, fmt, std::forward<Args>(args)...);
    }
    template <typename... Args> void info(fmt::format_string<Args...> fmt, Args&&... args)
    {
        write(Level::Info, fmt, std::forward<Args>(args)...);
    }
    template <typename... Args> void warn(fmt::format_string<Args...> fmt, Args&&... args)
    {
        write(Level::Warn, fmt, std::forward<Args>(args)...);
    }
    template <typename... Args> void error(fmt::format_string<Args...> fmt, Args&&... args)
    {
        write(Level::Error, fmt, std::forward<Args>(args)...);
    }

    // Runtime gate. Call from the module's debug-flag tick / hot-key
    // handler. When false, write() returns immediately.
    void set_enabled(bool e)
    {
        is_enabled = e;
    }
    bool enabled() const
    {
        return is_enabled;
    }

    void set_min_level(Level lv)
    {
        minimum_level = lv;
    }
    Level min_level() const
    {
        return minimum_level;
    }

    const std::string& name() const
    {
        return channel_name;
    }

    // Public so std::unique_ptr<Channel> in the registry can destroy
    // owned channels at process shutdown. Construction stays private:
    // all channels MUST be created via Channel::get() so the registry
    // is the single source of truth for channel identity.
    ~Channel();

  private:
    Channel(std::string name, Sink sink, const char* file_path, Level min_level,
            bool mirror_to_stderr);

    // The non-template, non-inline write path. Hidden in Log.cpp so
    // we keep the fmt + FILE includes out of the public header.
    void write_formatted(Level lv, const std::string& formatted);

    std::string channel_name;
    Sink sink_kind;
    std::FILE* file = nullptr;     // owned when sink_kind == File
    bool mirror_to_stderr = false; // File channels can also write to stderr
    bool is_enabled = true;
    Level minimum_level = Level::Trace;
};

} // namespace engine::log
