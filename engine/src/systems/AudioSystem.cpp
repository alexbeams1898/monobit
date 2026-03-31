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
static bool sMusicMuted = false;
static float sMusicRequestedVolume = 0.8f; // volume requested by caller (preserved across mute)

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

void AudioSystem::playSfx(const std::string& path, float volume, float pitch)
{
    if (!sInitialized)
        return;

    cleanupFinishedVoices();

    SfxVoice* slot = findFreeVoice();
    if (slot == nullptr)
        return; // pool full, drop the sound

    ma_result result =
        ma_sound_init_from_file(&sEngine, path.c_str(), MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_ASYNC,
                                nullptr, nullptr, &slot->sound);
    if (result != MA_SUCCESS)
    {
        std::cerr << "[AudioSystem] playSfx failed for: " << path << " (error " << result << ")\n";
        return;
    }

    ma_sound_set_volume(&slot->sound, volume);
    if (pitch != 1.0f)
        ma_sound_set_pitch(&slot->sound, pitch);
    ma_sound_start(&slot->sound);
    slot->active = true;
}

void AudioSystem::playMusic(const std::string& path, float volume, bool loop, int fade_in_ms)
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

    ma_sound_set_looping(&sMusicSound, loop ? MA_TRUE : MA_FALSE);
    sMusicRequestedVolume = volume;
    ma_sound_set_volume(&sMusicSound, sMusicMuted ? 0.0f : volume);
    ma_sound_seek_to_pcm_frame(&sMusicSound, 0);
    ma_sound_start(&sMusicSound);
    sMusicLoaded = true;

    // Apply fade-in after start so the sound is already running when the
    // fade begins. The fade overrides the volume set above.
    if (fade_in_ms > 0 && !sMusicMuted)
    {
        ma_sound_set_fade_in_milliseconds(&sMusicSound, 0.0f, volume,
                                          static_cast<ma_uint64>(fade_in_ms));
    }
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

float AudioSystem::getMusicVolume()
{
    if (!sInitialized || !sMusicLoaded)
        return 0.0f;

    return ma_sound_get_volume(&sMusicSound);
}

void AudioSystem::toggleMusicMute()
{
    sMusicMuted = !sMusicMuted;
    if (sInitialized && sMusicLoaded)
        ma_sound_set_volume(&sMusicSound, sMusicMuted ? 0.0f : sMusicRequestedVolume);
}

bool AudioSystem::isMusicMuted()
{
    return sMusicMuted;
}

void AudioSystem::setMasterVolume(float volume)
{
    if (!sInitialized)
        return;

    ma_engine_set_volume(&sEngine, volume);
}
