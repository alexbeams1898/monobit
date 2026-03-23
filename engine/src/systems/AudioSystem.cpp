// Two-phase stb_vorbis include for miniaudio OGG/Vorbis support.
// Phase 1: header-only (gives miniaudio the function declarations).
#define STB_VORBIS_HEADER_ONLY
#include <stb_vorbis.c> // NOLINT(bugprone-suspicious-include)

// MINIAUDIO_IMPLEMENTATION must be defined in exactly one translation unit.
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

// Phase 2: full implementation (after miniaudio, so macros don't leak).
#undef STB_VORBIS_HEADER_ONLY
#include "systems/AudioSystem.h"

#include <iostream>
#include <stb_vorbis.c> // NOLINT(bugprone-suspicious-include)
#include <vector>

// ---------------------------------------------------------------------------
// Static state
// ---------------------------------------------------------------------------

static ma_engine sEngine;
static bool sInitialized = false;

static ma_sound sMusicSound;
static bool sMusicLoaded = false;

// Managed SFX voices -- each playSfx creates a ma_sound with per-sound volume.
// Finished sounds are cleaned up lazily on the next playSfx call.
static constexpr int MAX_SFX_VOICES = 32;

struct SfxVoice
{
    ma_sound sound;
    bool active = false;
};
static std::vector<SfxVoice> sSfxVoices(MAX_SFX_VOICES);

// Sweep the voice pool and uninit any sounds that have finished playing.
static void cleanupFinishedVoices()
{
    for (auto& v : sSfxVoices)
    {
        if (v.active && !ma_sound_is_playing(&v.sound))
        {
            ma_sound_uninit(&v.sound);
            v.active = false;
        }
    }
}

// Find an inactive slot. Returns nullptr if the pool is full.
static SfxVoice* findFreeVoice()
{
    for (auto& v : sSfxVoices)
    {
        if (!v.active)
            return &v;
    }
    return nullptr;
}

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

    // Clean up all active SFX voices.
    for (auto& v : sSfxVoices)
    {
        if (v.active)
        {
            ma_sound_uninit(&v.sound);
            v.active = false;
        }
    }

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

    cleanupFinishedVoices();

    SfxVoice* slot = findFreeVoice();
    if (slot == nullptr)
        return; // pool full, drop the sound

    ma_result result = ma_sound_init_from_file(&sEngine, path.c_str(), MA_SOUND_FLAG_DECODE,
                                               nullptr, nullptr, &slot->sound);
    if (result != MA_SUCCESS)
    {
        std::cerr << "[AudioSystem] playSfx failed for: " << path << " (error " << result << ")\n";
        return;
    }

    ma_sound_set_volume(&slot->sound, volume);
    ma_sound_start(&slot->sound);
    slot->active = true;
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

    ma_result result = ma_sound_init_from_file(&sEngine, path.c_str(), MA_SOUND_FLAG_DECODE,
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
