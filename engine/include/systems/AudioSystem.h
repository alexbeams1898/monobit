#pragma once

#include <string>

// ---------------------------------------------------------------------------
// AudioSystem — thin wrapper around miniaudio's ma_engine.
//
// Design for scale:
//   SFX   — fire-and-forget via ma_engine_play_sound().  The engine manages
//             a pool of concurrent voices internally.  Safe to call 1000x/s.
//   Music — one looping ma_sound instance.  Swap tracks with playMusic().
//             Fade-in/out can be added later via ma_sound_set_volume().
//
// Volume hierarchy:
//   master → music / sfx (separate volume controls planned — currently shared
//            via master until group support is added).
// ---------------------------------------------------------------------------

class AudioSystem
{
  public:
    // Initialize audio device and miniaudio engine.
    // Returns false if the device cannot be opened (e.g. no audio hardware);
    // all subsequent calls become no-ops so the game still runs without sound.
    static bool init();

    static void shutdown();

    // Fire-and-forget sound effect.  Engine manages voice lifetime.
    // volume: 0.0 = silent, 1.0 = full.  pitch: 1.0 = normal, >1 = higher/faster.
    static void playSfx(const std::string& path, float volume = 1.0f, float pitch = 1.0f);

    // Start background music.  Replaces any currently playing track.
    // volume: 0.0 = silent, 1.0 = full.  loop: true = repeat, false = one-shot.
    static void playMusic(const std::string& path, float volume = 0.8f, bool loop = true);

    static void stopMusic();
    static void setMusicVolume(float volume); // 0.0–1.0
    static float getMusicVolume();
    static void toggleMusicMute(); // toggle mute; persists across track changes
    static bool isMusicMuted();
    static void setMasterVolume(float volume); // 0.0–1.0
};
