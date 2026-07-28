#include "Ambience.h"

#include "JsonConfig.h"
#include "systems/AudioSystem.h"

#include <nlohmann/json.hpp>

#include <cstdio>

namespace ambience
{

void load(Config& cfg, const std::string& path)
{
    cfg.channels.clear();
    const auto loaded = config::load(path);
    if (!loaded)
        return;
    const nlohmann::json& j = *loaded;
    if (const auto it = j.find("channels"); it != j.end() && it->is_object())
        for (const auto& [name, c] : it->items())
        {
            Channel ch;
            ch.path = c.value("path", std::string{});
            ch.volume = c.value("volume", 1.0f);
            ch.loop = c.value("loop", false);
            ch.stop_on_flag = c.value("stop_on_flag", std::string{});
            ch.start_on_flag = c.value("start_on_flag", std::string{});
            ch.fade_ms = c.value("fade_ms", 250);
            ch.fade_in_ms = c.value("fade_in_ms", 0);
            if (!ch.path.empty())
                cfg.channels[name] = std::move(ch);
        }
}

void start(State& st, const Config& cfg, const std::string& name)
{
    const auto it = cfg.channels.find(name);
    if (it == cfg.channels.end())
    {
        std::fprintf(stderr, "[ambience] no channel '%s' (config/ambience.json)\n", name.c_str());
        return;
    }
    const Channel& ch = it->second;
    if (!ch.loop)
    {
        AudioSystem::playSfx(ch.path, ch.volume); // one-shot: fire and forget
        return;
    }
    if (st.playing.count(name) > 0)
        return; // already sounding
    const int voice =
        AudioSystem::playSfxTracked(ch.path, ch.volume, 1.0f, /*loop=*/true, ch.fade_in_ms);
    if (voice >= 0)
        st.playing[name] = voice;
}

namespace
{
int fadeOf(const Config& cfg, const std::string& name)
{
    const auto it = cfg.channels.find(name);
    return it != cfg.channels.end() ? it->second.fade_ms : 250;
}
} // namespace

void stop(State& st, const Config& cfg, const std::string& name)
{
    const auto it = st.playing.find(name);
    if (it == st.playing.end())
        return;
    AudioSystem::stopSfx(it->second, fadeOf(cfg, name));
    st.playing.erase(it);
}

void stopAll(State& st, const Config& cfg)
{
    for (const auto& [name, voice] : st.playing)
        AudioSystem::stopSfx(voice, fadeOf(cfg, name));
    st.playing.clear();
}

void arm(State& st, const Config& cfg, const std::unordered_set<std::string>& flags)
{
    for (const auto& [name, ch] : cfg.channels)
        if (!ch.start_on_flag.empty() && flags.count(ch.start_on_flag) > 0)
            st.flag_started.insert(name);
}

void tick(State& st, const Config& cfg, const std::unordered_set<std::string>& flags)
{
    // Stops: a playing channel whose stop_on_flag is now held fades out.
    for (auto it = st.playing.begin(); it != st.playing.end();)
    {
        const auto ch = cfg.channels.find(it->first);
        const bool dead = ch != cfg.channels.end() && !ch->second.stop_on_flag.empty() &&
                          flags.count(ch->second.stop_on_flag) > 0;
        if (dead)
        {
            AudioSystem::stopSfx(it->second, ch->second.fade_ms);
            it = st.playing.erase(it);
        }
        else
        {
            ++it;
        }
    }
    // Starts: a channel whose start_on_flag just appeared fires, once per walk --
    // the TV's shutdown clunk arrives WITH the flag the deed set.
    for (const auto& [name, ch] : cfg.channels)
    {
        if (ch.start_on_flag.empty() || st.flag_started.count(name) > 0 ||
            flags.count(ch.start_on_flag) == 0)
            continue;
        st.flag_started.insert(name);
        start(st, cfg, name);
    }
}

} // namespace ambience
