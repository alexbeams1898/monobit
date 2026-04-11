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

// Low-pass filter node sitting between music sounds and the engine endpoint.
// Always inserted in the chain; "disabled" by reinit with a near-Nyquist cutoff
// so it's effectively transparent. This avoids re-attaching nodes at runtime.
static ma_lpf_node sMusicLpfNode;
static bool sMusicLpfReady = false;
static float sMusicLpfCurrentCutoff = 0.0f; // 0 = bypass (transparent)
static constexpr float MUSIC_LPF_BYPASS_HZ = 20000.0f;
static constexpr ma_uint32 MUSIC_LPF_ORDER = 4;

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
    const ma_result result = ma_engine_init(nullptr, &sEngine);
    if (result != MA_SUCCESS)
    {
        std::cerr << "[AudioSystem] Failed to initialize audio engine (error " << result
                  << "). Running without sound.\n";
        return false;
    }

    sInitialized = true;

    // Insert a low-pass filter node into the music chain. Initialized at a
    // near-Nyquist cutoff so it's effectively transparent until enabled by
    // setMusicLowPass(). Music sounds attach to this node instead of directly
    // to the endpoint.
    const ma_uint32 channels = ma_engine_get_channels(&sEngine);
    const ma_uint32 sampleRate = ma_engine_get_sample_rate(&sEngine);
    ma_lpf_node_config lpfCfg =
        ma_lpf_node_config_init(channels, sampleRate, MUSIC_LPF_BYPASS_HZ, MUSIC_LPF_ORDER);
    const ma_result lpfResult = ma_lpf_node_init(ma_engine_get_node_graph(&sEngine), &lpfCfg,
                                                 nullptr, &sMusicLpfNode);
    if (lpfResult == MA_SUCCESS)
    {
        // Attach the LPF node's output to the engine endpoint so its processed
        // audio reaches the device.
        ma_node_attach_output_bus(&sMusicLpfNode, 0, ma_engine_get_endpoint(&sEngine), 0);
        sMusicLpfReady = true;
        sMusicLpfCurrentCutoff = 0.0f;
    }
    else
    {
        std::cerr << "[AudioSystem] Failed to init music low-pass node (error " << lpfResult
                  << "). Music will play without filter.\n";
    }

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

    if (sMusicLpfReady)
    {
        ma_lpf_node_uninit(&sMusicLpfNode, nullptr);
        sMusicLpfReady = false;
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

    const ma_result result =
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

int AudioSystem::playSfxTracked(const std::string& path, float volume, float pitch, bool loop)
{
    if (!sInitialized)
        return -1;

    cleanupFinishedVoices();

    // Find free slot and return its index.
    for (int i = 0; i < static_cast<int>(sSfxVoices.size()); ++i)
    {
        if (sSfxVoices[i].active)
            continue;

        const ma_result result = ma_sound_init_from_file(&sEngine, path.c_str(),
                                                         MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_ASYNC,
                                                         nullptr, nullptr, &sSfxVoices[i].sound);
        if (result != MA_SUCCESS)
        {
            std::cerr << "[AudioSystem] playSfxTracked failed for: " << path << " (error " << result
                      << ")\n";
            return -1;
        }

        ma_sound_set_volume(&sSfxVoices[i].sound, volume);
        if (pitch != 1.0f)
            ma_sound_set_pitch(&sSfxVoices[i].sound, pitch);
        ma_sound_set_looping(&sSfxVoices[i].sound, loop ? MA_TRUE : MA_FALSE);
        ma_sound_start(&sSfxVoices[i].sound);
        sSfxVoices[i].active = true;
        return i;
    }

    return -1; // pool full
}

void AudioSystem::stopSfx(int voice_index, int fade_ms)
{
    if (!sInitialized)
        return;
    if (voice_index < 0 || voice_index >= static_cast<int>(sSfxVoices.size()))
        return;

    auto& v = sSfxVoices[voice_index];
    if (!v.active)
        return;

    if (fade_ms > 0)
    {
        // Fade to silence, then stop.
        ma_sound_set_fade_in_milliseconds(&v.sound, -1.0f, 0.0f, static_cast<ma_uint64>(fade_ms));
        ma_sound_set_stop_time_in_milliseconds(&v.sound, static_cast<ma_uint64>(fade_ms));
    }
    else
    {
        ma_sound_stop(&v.sound);
        ma_sound_uninit(&v.sound);
        v.active = false;
    }
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

    const ma_result result = ma_sound_init_from_file(&sEngine, path.c_str(), MA_SOUND_FLAG_STREAM,
                                                     nullptr, nullptr, &sMusicSound);
    if (result != MA_SUCCESS)
    {
        std::cerr << "[AudioSystem] playMusic failed for: " << path << " (error " << result
                  << ")\n";
        return;
    }

    // Route music through the low-pass node instead of straight to the endpoint
    // so setMusicLowPass() can muffle it. Reset the LPF to bypass on every new
    // track so a lingering muffle from a previous screen doesn't carry over
    // (e.g. menu-open -> escape run -> victory track inherits the muffle).
    if (sMusicLpfReady)
    {
        ma_node_attach_output_bus(&sMusicSound, 0, &sMusicLpfNode, 0);
        const ma_uint32 lpfChannels = ma_engine_get_channels(&sEngine);
        const ma_uint32 lpfSampleRate = ma_engine_get_sample_rate(&sEngine);
        const ma_lpf_config lpfCfg = ma_lpf_config_init(
            ma_format_f32, lpfChannels, lpfSampleRate, MUSIC_LPF_BYPASS_HZ, MUSIC_LPF_ORDER);
        ma_lpf_node_reinit(&lpfCfg, &sMusicLpfNode);
        sMusicLpfCurrentCutoff = 0.0f;
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

void AudioSystem::setMusicLowPass(float cutoff_hz)
{
    if (!sInitialized || !sMusicLpfReady)
        return;

    // Treat anything <= 0 as "bypass": reinit the LPF at near-Nyquist so it has
    // no audible effect, and remember that we're in bypass mode.
    const float requested = (cutoff_hz <= 0.0f) ? 0.0f : cutoff_hz;
    if (requested == sMusicLpfCurrentCutoff)
        return; // no-op when already at the requested setting

    const float effective = (requested <= 0.0f) ? MUSIC_LPF_BYPASS_HZ : requested;
    const ma_uint32 channels = ma_engine_get_channels(&sEngine);
    const ma_uint32 sampleRate = ma_engine_get_sample_rate(&sEngine);
    const ma_lpf_config cfg =
        ma_lpf_config_init(ma_format_f32, channels, sampleRate, effective, MUSIC_LPF_ORDER);
    if (ma_lpf_node_reinit(&cfg, &sMusicLpfNode) == MA_SUCCESS)
        sMusicLpfCurrentCutoff = requested;
}
