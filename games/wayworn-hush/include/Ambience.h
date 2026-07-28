#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>

// Ambient sound channels: NAMED sounds the world plays -- a droning alarm, the
// TV's murmur, a knock. Config (config/ambience.json) owns what each channel is
// (file, volume, looping); scenes start them by name (the `sound` step) and the
// world stops them diegetically: a channel may declare `stop_on_flag`, and the
// moment that flag exists the sound dies -- so "turn off the TV" is an ordinary
// deed setting an ordinary flag, and the audio follows the knowledge system like
// everything else. No system ever holds a voice handle but this one.
namespace ambience
{

struct Channel
{
    std::string path;
    float volume = 1.0f;
    bool loop = false;
    std::string stop_on_flag;  // channel dies when this flag exists (empty = never)
    std::string start_on_flag; // channel fires (once) when this flag appears -- the
                               // TV's shutdown clunk the moment tv_off is set
    int fade_ms = 250;         // stop fade -- birds drift out slowly, a TV pops off
    int fade_in_ms = 0;        // start fade (loops only) -- the world eases in
};

struct Config
{
    std::unordered_map<std::string, Channel> channels;
};

// Runtime: which channels are sounding (name -> engine voice), and which
// start_on_flag channels already fired (once per walk).
struct State
{
    std::unordered_map<std::string, int> playing;
    std::unordered_set<std::string> flag_started;
};

// Load config/ambience.json (silent no-op -> empty config if missing).
void load(Config& cfg, const std::string& path);

// Start a channel by name (no-op if unknown -- logged -- or already sounding).
void start(State& st, const Config& cfg, const std::string& name);

// Stop one channel / everything (region switches silence the old room), fading
// per the channel's fade_ms.
void stop(State& st, const Config& cfg, const std::string& name);
void stopAll(State& st, const Config& cfg);

// Arm a fresh walk's state against the RESTORED flags: any start_on_flag already
// held is marked fired without playing. The sound belongs to the moment its flag
// LANDS -- resuming a save must never replay the past (the same rule the
// stat-change fingerprint follows). Call at world-enter, after the save is applied.
void arm(State& st, const Config& cfg, const std::unordered_set<std::string>& flags);

// The flag bus: kill any playing channel whose stop_on_flag is now held, and fire
// (once) any channel whose start_on_flag just appeared. Called once per tick with
// the live flag set.
void tick(State& st, const Config& cfg, const std::unordered_set<std::string>& flags);

} // namespace ambience
