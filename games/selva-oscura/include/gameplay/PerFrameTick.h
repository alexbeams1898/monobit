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

// Full session-boundary reset. SINGLE source of truth for "fresh
// state for this character." Called on New Game / Load Game /
// character switch. Resets cinematic scene, sampler, bosses, enemy
// pool, doors, and player actor in canonical order. Adding a new
// per-character state holder means adding ONE line to its body.
void hardResetWorldForCharacter(const selva::PlayerProfile& profile);

// Mid-character cycle-boundary reset. Called on player death/respawn.
// Re-streams enemies into spawn positions per "Per-circle reactivity"
// (setting.md). Tighter scope than the hard reset: player is alive,
// no cinematic in flight, doors persist mid-cycle.
void softResetWorldForCycle();

// Symmetric write-back: copy the player Actor's persistent fields out
// to the given PlayerProfile. Called by the pause-menu Save action and
// by Quit-to-Main-Menu before SaveManager writes to disk, so the saved
// file reflects what was earned in the run. There is nothing to write
// yet: PlayerProfile carries only `name`, which cannot change at runtime.
void saveActiveCharacterFromPlayer(selva::PlayerProfile& profile);

// Begin the wake-up Scene: lock combat + movement, fire the getting_up
// one-shot on the player. The Scene auto-ends when the one-shot
// completes (handled per-frame inside selvaPerFrame). Called from the
// New-Game flow only -- respawns place the Vagrant standing.
void beginWakeScene();

// Clears wake-scene runtime tracking. Call on Playing-exit so the
// watcher doesn't carry across a character switch.
void resetWakeSceneTracking();

// Input gating. Single source of truth for "is gameplay input
// suppressed right now." Composes Scene locks, dialog, tuning panel,
// pause menu, etc. Every gameplay-input site MUST go through these
// instead of OR-ing flags inline -- without this contract, each new
// UI overlay needs a hand-edit at every input check (the bug class
// that left mouse-look firing during dialog).
bool gameplayLookSuppressed();     // camera mouse-look
bool gameplayCombatSuppressed();   // LMB/RMB attacks, dodge, jump
bool gameplayMovementSuppressed(); // WASD locomotion

// Render-world entry point. Frame capture readback lives here too
// (uses tickstate::frameCapture* flags).
void selvaRenderWorld(::Engine& engine, ::EntityManager& em, float camX, float camY, float alpha);

} // namespace selva::gameplay
