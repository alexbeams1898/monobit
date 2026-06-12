#include "audio/Audio.h"

#include "WallClock.h"
#include "systems/AudioSystem.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <random>
#include <unordered_map>
#include <vector>

namespace selva::audio
{

namespace
{

struct SoundEntry
{
    // `path` is the primary file; if `variations` is non-empty, playSfx
    // picks one at random each call (matches prison-escape-game's footstep
    // variation pattern - see games/prison-escape-game/config/audio/sounds.json).
    // For sounds with no variations, just leave the array empty and the
    // single `path` plays.
    std::string path;
    std::vector<std::string> variations;
    float volume = 1.0f;
    float peak_offset_seconds = 0.0f;
};

struct ScheduledEntry
{
    std::string name;
    float play_at = 0.0f;
};

struct MusicConfig
{
    std::string path;
    float volume = 0.35f;
    bool loop = true;
    int fade_in_ms = 0;
    float lowpass_during_death_hz = 700.0f;
};

std::unordered_map<std::string, SoundEntry> sSounds;
std::vector<ScheduledEntry> sScheduled;
MusicConfig sMusic;
// Named music beds (encounter swaps). Populated from audio.json's
// "music_beds" object at init time. Empty if none authored.
std::unordered_map<std::string, MusicConfig> sNamedBeds;
// Stack of previously-active beds for pushMusicBed/popMusicBed.
// Each entry is a snapshot of `sMusic` at push-time; popping
// restores it.
std::vector<MusicConfig> sBedStack;
bool sAudioReady = false;

} // namespace

namespace
{
// Load a single SFX entry from JSON. Skips invalid entries (returns
// false without inserting).
bool loadOneSfxEntry(const nlohmann::json& entry, SoundEntry& out)
{
    if (!entry.is_object())
        return false;
    out.path = entry.value("path", std::string());
    out.volume = entry.value("volume", 1.0f);
    out.peak_offset_seconds = entry.value("peak_offset_seconds", 0.0f);
    if (entry.contains("variations") && entry["variations"].is_array())
    {
        for (const auto& v : entry["variations"])
        {
            if (v.is_string())
                out.variations.push_back(v.get<std::string>());
        }
    }
    return !(out.path.empty() && out.variations.empty());
}

void loadSfxRegistry(const nlohmann::json& doc)
{
    if (!doc.contains("sfx") || !doc["sfx"].is_object())
        return;
    const auto& sfx = doc["sfx"];
    for (auto it = sfx.begin(); it != sfx.end(); ++it)
    {
        SoundEntry se;
        if (loadOneSfxEntry(it.value(), se))
            sSounds.emplace(it.key(), std::move(se));
    }
}

// Parse a MusicConfig from a JSON object (the "music" entry's shape).
// Default-on-missing values mirror the original loadMusic defaults.
MusicConfig parseMusicConfig(const nlohmann::json& m)
{
    MusicConfig mc;
    mc.path = m.value("path", std::string());
    mc.volume = m.value("volume", 0.35f);
    mc.loop = m.value("loop", true);
    mc.fade_in_ms = m.value("fade_in_ms", 0);
    mc.lowpass_during_death_hz = m.value("lowpass_during_death_hz", 700.0f);
    return mc;
}

void loadMusic(const nlohmann::json& doc)
{
    // Named beds first (so push/pop has a registry even if there's no
    // ambient bed). Per docs/design/ideas/boss_backend.md section 9.
    if (doc.contains("music_beds") && doc["music_beds"].is_object())
    {
        const auto& beds = doc["music_beds"];
        for (auto it = beds.begin(); it != beds.end(); ++it)
        {
            if (!it.value().is_object())
                continue;
            sNamedBeds.emplace(it.key(), parseMusicConfig(it.value()));
        }
        std::fprintf(stderr, "[audio] loaded %zu named music bed(s)\n", sNamedBeds.size());
    }

    if (!doc.contains("music") || !doc["music"].is_object())
        return;
    sMusic = parseMusicConfig(doc["music"]);
    if (sMusic.path.empty())
        return;
    AudioSystem::playMusic(sMusic.path, sMusic.volume, sMusic.loop, sMusic.fade_in_ms);
    std::fprintf(stderr, "[audio] music start path='%s' vol=%.2f loop=%d fade=%dms\n",
                 sMusic.path.c_str(), sMusic.volume, sMusic.loop ? 1 : 0, sMusic.fade_in_ms);
}
} // namespace

bool init(const std::string& registry_path)
{
    // AudioSystem is initialized inside Engine::init() before this
    // runs. Re-initing miniaudio's device thrashes the engine's already-
    // running voices and (on Windows) leaves it silently broken. Just
    // trust engine init succeeded and load the registry on top.
    sAudioReady = true;

    sSounds.clear();
    std::ifstream f(registry_path);
    if (!f.good())
    {
        std::fprintf(stderr, "[audio] no registry at %s — no sounds will play\n",
                     registry_path.c_str());
        return sAudioReady;
    }
    nlohmann::json doc;
    try
    {
        f >> doc;
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "[audio] failed to parse %s: %s\n", registry_path.c_str(), e.what());
        return sAudioReady;
    }
    if (!doc.is_object())
        return sAudioReady;

    loadSfxRegistry(doc);
    loadMusic(doc);

    std::fprintf(stderr, "[audio] loaded %zu sfx + music='%s' from %s\n", sSounds.size(),
                 sMusic.path.c_str(), registry_path.c_str());
    return sAudioReady;
}

void shutdown()
{
    AudioSystem::shutdown();
    sAudioReady = false;
    sSounds.clear();
    sScheduled.clear();
    sMusic = MusicConfig{};
}

void duckMusic()
{
    if (sMusic.path.empty())
        return;
    AudioSystem::setMusicLowPass(sMusic.lowpass_during_death_hz);
}

void restoreMusic()
{
    if (sMusic.path.empty())
        return;
    // cutoff <= 0 disables the filter; passing 0 returns the engine
    // to bypass (transparent).
    AudioSystem::setMusicLowPass(0.0f);
}

void pushMusicBed(const std::string& name)
{
    if (!sAudioReady)
        return;
    auto it = sNamedBeds.find(name);
    if (it == sNamedBeds.end())
    {
        std::fprintf(stderr, "[audio] pushMusicBed: bed '%s' not registered (no swap)\n",
                     name.c_str());
        return;
    }
    // Push current bed onto the stack, then start the named one.
    sBedStack.push_back(sMusic);
    sMusic = it->second;
    if (sMusic.path.empty())
    {
        std::fprintf(stderr, "[audio] pushMusicBed: bed '%s' has empty path; silence\n",
                     name.c_str());
        return;
    }
    AudioSystem::playMusic(sMusic.path, sMusic.volume, sMusic.loop, sMusic.fade_in_ms);
    std::fprintf(stderr, "[audio] pushMusicBed '%s' (path='%s')\n", name.c_str(),
                 sMusic.path.c_str());
}

void popMusicBed()
{
    if (sBedStack.empty())
        return;
    sMusic = sBedStack.back();
    sBedStack.pop_back();
    if (sMusic.path.empty())
        return;
    AudioSystem::playMusic(sMusic.path, sMusic.volume, sMusic.loop, sMusic.fade_in_ms);
    std::fprintf(stderr, "[audio] popMusicBed -> '%s'\n", sMusic.path.c_str());
}

void playSfxInternal(const std::string& name, float gain)
{
    if (!sAudioReady)
    {
        std::fprintf(stderr, "[audio] playSfx('%s') — audio not ready, skipping\n", name.c_str());
        return;
    }
    const auto it = sSounds.find(name);
    if (it == sSounds.end())
    {
        std::fprintf(stderr, "[audio] playSfx('%s') — not in registry\n", name.c_str());
        return;
    }
    std::string path = it->second.path;
    if (!it->second.variations.empty())
    {
        static std::mt19937 s_rng{std::random_device{}()};
        std::uniform_int_distribution<std::size_t> dist(0, it->second.variations.size() - 1);
        path = it->second.variations[dist(s_rng)];
    }
    AudioSystem::playSfx(path, it->second.volume * gain);
}

void playSfx(const std::string& name)
{
    playSfxInternal(name, 1.0f);
}

void playSfxScaled(const std::string& name, float gain)
{
    playSfxInternal(name, gain);
}

void scheduleSfx(const std::string& name, float play_at_wallclock)
{
    sScheduled.push_back({name, play_at_wallclock});
}

float sfxPeakOffset(const std::string& name)
{
    const auto it = sSounds.find(name);
    return (it != sSounds.end()) ? it->second.peak_offset_seconds : 0.0f;
}

void tickScheduledSfx()
{
    if (sScheduled.empty())
        return;
    const float now = selva::wallClock();
    auto it = sScheduled.begin();
    while (it != sScheduled.end())
    {
        if (it->play_at <= now)
        {
            playSfx(it->name);
            it = sScheduled.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

} // namespace selva::audio
