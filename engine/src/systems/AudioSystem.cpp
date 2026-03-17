// MINIAUDIO_IMPLEMENTATION must be defined in exactly one translation unit.
#define MINIAUDIO_IMPLEMENTATION
#include "systems/AudioSystem.h"

#include <iostream>
#include <miniaudio.h>

// ---------------------------------------------------------------------------
// Static state
// ---------------------------------------------------------------------------

static ma_engine sEngine;
static bool sInitialized = false;

static ma_sound sMusicSound;
static bool sMusicLoaded = false;

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool AudioSystem::init()
{
    ma_result result = ma_engine_init(nullptr, &sEngine);
    if (result != MA_SUCCESS)
    {
        std::cerr << "[AudioSystem] Failed to initialize audio engine (error " << result
                  << "). Running without sound.\n";
        return false;
    }

    sInitialized = true;
    std::cout << "[AudioSystem] Audio engine initialized.\n";
    return true;
}

void AudioSystem::shutdown()
{
    if (!sInitialized)
        return;

    if (sMusicLoaded)
    {
        ma_sound_uninit(&sMusicSound);
        sMusicLoaded = false;
    }

    ma_engine_uninit(&sEngine);
    sInitialized = false;
}

void AudioSystem::playSfx(const std::string& path, float volume)
{
    if (!sInitialized)
        return;

    // ma_engine_play_sound is fire-and-forget: the engine allocates an internal
    // voice, plays to completion, and frees it automatically.  Safe at high call rates.
    ma_result result = ma_engine_play_sound(&sEngine, path.c_str(), nullptr);
    if (result != MA_SUCCESS)
    {
        std::cerr << "[AudioSystem] playSfx failed for: " << path << " (error " << result << ")\n";
        return;
    }

    // volume per-sound is not directly available on fire-and-forget calls;
    // use setMasterVolume() or upgrade to ma_sound for per-sound control.
    (void)volume;
}

void AudioSystem::playMusic(const std::string& path, float volume)
{
    if (!sInitialized)
        return;

    // Tear down any currently playing track first.
    if (sMusicLoaded)
    {
        ma_sound_uninit(&sMusicSound);
        sMusicLoaded = false;
    }

    ma_result result = ma_sound_init_from_file(&sEngine, path.c_str(), MA_SOUND_FLAG_STREAM,
                                               nullptr, nullptr, &sMusicSound);
    if (result != MA_SUCCESS)
    {
        std::cerr << "[AudioSystem] playMusic failed for: " << path << " (error " << result
                  << ")\n";
        return;
    }

    ma_sound_set_looping(&sMusicSound, MA_TRUE);
    ma_sound_set_volume(&sMusicSound, volume);
    ma_sound_start(&sMusicSound);
    sMusicLoaded = true;
}

void AudioSystem::stopMusic()
{
    if (!sInitialized || !sMusicLoaded)
        return;

    ma_sound_stop(&sMusicSound);
}

void AudioSystem::setMusicVolume(float volume)
{
    if (!sInitialized || !sMusicLoaded)
        return;

    ma_sound_set_volume(&sMusicSound, volume);
}

void AudioSystem::setMasterVolume(float volume)
{
    if (!sInitialized)
        return;

    ma_engine_set_volume(&sEngine, volume);
}
