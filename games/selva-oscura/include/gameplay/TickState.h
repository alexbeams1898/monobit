#pragma once

#include <cstddef>
#include <string>

// Per-frame mutable state owned by gameplay/PerFrameTick.cpp.
// Exposed here so the tuning panel (ui/TuningPanel.cpp) can read +
// toggle the F1-armed debug flags without going through awkward
// callback plumbing. These would be private statics if not for the
// tuning UI's need to drive them.
namespace selva::gameplay::tickstate
{

// In-game tuning panel toggle. F1 flips it.
bool& showTuningPanel();

// Debug clip preview: when non-empty, the per-frame update bypasses
// gameplay state machines and feeds the named clip straight into
// the sampler. F1 panel sets / clears.
std::string& debugClipName();
bool& debugLoop();

// CSV bone recording for the in-flight debug clip. The F1 panel
// arms / fires / inspects these.
bool& debugRecording();
float& debugRecordElapsed();
float& debugRecordDuration();
std::string& debugRecordClipName();

// "Armed" flag: next dodge fire starts a CSV recording.
bool& debugRecordArmedNextDodge();

// Frame screenshot capture flags — F1 arms; per-frame logic clears
// when capture starts.
bool& frameCaptureArmedNextDodge();
bool& frameCaptureArmedNextChain();

// Frame-capture in-flight progress (read-only from the panel).
bool frameCaptureActive();
float frameCaptureElapsed();
float frameCaptureDuration();
int frameCaptureCounter();

// Debug-record sample count (read-only from the panel).
std::size_t debugRecordSampleCount();

// Reset the debug-record samples vector (clear + reserve) before
// starting a new recording. Called from the panel.
void resetDebugRecordSamples(std::size_t expected);

} // namespace selva::gameplay::tickstate
