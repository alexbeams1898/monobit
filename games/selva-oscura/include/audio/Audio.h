#pragma once

#include <string>

namespace selva::audio
{

// Initialize the underlying audio engine (miniaudio via AudioSystem)
// and load the sound registry from JSON. Safe to call once at
// startup; returns false if the audio device is unavailable (game
// runs silently in that case — no fatal).
bool init(const std::string& registry_path);

// Tear down miniaudio. Called once at shutdown.
void shutdown();

// Fire-and-forget a sound by registry name. No-op if name is not
// registered or audio failed to init. The underlying engine manages
// voice lifetime, so this is safe to call from any per-frame system.
void playSfx(const std::string& name);

// As playSfx, but scales the registered volume by `gain` (1.0 = full
// registered volume, 0.0 = silent). Used for impact-velocity-driven
// dynamics in the footstep detector where a single SFX bank serves
// a range of foot-plant intensities.
void playSfxScaled(const std::string& name, float gain);

// Queue a sound to fire at a future wall-clock time. Used for
// layered audio where one sound's peak must align with a visual
// event (e.g. the second-death card lands on the synth-echo's
// loudest beat). Drained each frame by tickScheduledSfx().
void scheduleSfx(const std::string& name, float play_at_wallclock);

// Per-frame drain of the scheduled queue. Plays anything whose
// play_at_wallclock has elapsed. Idempotent if called multiple
// times in one frame (entries are removed on play).
void tickScheduledSfx();

// Peak-offset (seconds into the file) for `name`. Used by callers
// that need a visual event to align with the loudest beat of an
// SFX — schedule the play_at_time so that play_at + peak_offset =
// the target visual moment. Returns 0 if the name is unregistered
// or no peak_offset_seconds was declared.
float sfxPeakOffset(const std::string& name);

// Music ducking. Enables the engine's music low-pass filter to
// muffle the OST during a high-impact moment (the second-death
// card landing, future boss intros, etc.) so the foreground SFX
// reads clearly. duckMusic() applies the configured cutoff from
// audio.json; restoreMusic() returns to bypass. Cheap no-ops if
// no music is loaded.
void duckMusic();
void restoreMusic();

} // namespace selva::audio
