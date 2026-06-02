#pragma once

class Engine;
class EntityManager;

namespace selva
{
struct PlayerProfile;
} // namespace selva

namespace selva::gameplay
{

// Per-frame entry point invoked by Engine. Owns input handling,
// combat state machines, dodge/block, locomotion clip pick, sampler
// update, root-motion application. Implementation lives in
// src/gameplay/PerFrameTick.cpp.
void selvaPerFrame(::Engine& engine, ::EntityManager& em, double dt);

// Sample the current SDL mouse-button state into the file-static
// "previous button state" used for edge detection in selvaPerFrame.
// Call this from the gated-perframe path on frames where selvaPerFrame
// is SKIPPED (pause menu open). Without it, sPrevLMB / sPrevRMB go
// stale during pause and the resume frame sees a fresh press-edge for
// any button that was held when the menu opened or closed, triggering
// a spurious combat action (e.g. RMB-block firing when RMB closes the
// pause menu).
void syncInputEdgesFromCurrentState();

// Materialize an active character's profile into the live world. Owns
// the entire load-character contract: player actor (pos/yaw/HP/clear
// combat state), enemy pool (cycle-reset + apply profile.felled_bosses),
// doors (apply profile.door_states or revert to JSON initial_state).
//
// Called on every Playing-enter (New Game or Load Game). Every piece
// of profile state that maps into world state MUST flow through this
// function -- no scattered "applyX" calls in main.cpp. When a new
// per-character field lands on PlayerProfile, the application step
// goes here so character-switch reconciliation stays in one place.
void loadActiveCharacterIntoWorld(const selva::PlayerProfile& profile);

// Symmetric write-back: copy the player Actor's persistent fields out
// to the given PlayerProfile. Called by the pause-menu Save action and
// by Quit-to-Main-Menu before SaveManager writes to disk, so the saved
// file reflects what was earned in the run. v1 has nothing to write
// (PlayerProfile only carries `name`, which doesn't change at runtime).
void saveActiveCharacterFromPlayer(selva::PlayerProfile& profile);

// Begin the wake-up Scene: lock combat + movement, fire the getting_up
// one-shot on the player. The Scene auto-ends when the one-shot
// completes (handled per-frame inside selvaPerFrame). Called from the
// New-Game flow only -- respawns place the Vagrant standing.
void beginWakeScene();

// Clears wake-scene runtime tracking. Call on Playing-exit so the
// watcher doesn't carry across a character switch.
void resetWakeSceneTracking();

// Render-world entry point. Frame capture readback lives here too
// (uses tickstate::frameCapture* flags).
void selvaRenderWorld(::Engine& engine, ::EntityManager& em, float camX, float camY, float alpha);

} // namespace selva::gameplay
