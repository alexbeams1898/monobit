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

// Materialize the active character's persistent state from a PlayerProfile
// into the player Actor, AND set runtime-only fields (position from
// terrain spawn config, full hp, cleared combat / lock / one-shot state).
//
// Called on every Playing-enter (New Game or Load Game) so the runtime
// player starts from a clean baseline plus whatever the profile says
// the character knows / has earned. Both new and loaded characters
// always spawn at the configured spawn point; position is NOT
// persistent in v1 (the soulslike convention is to respawn at a save
// point, not at the quit location). When persistent-character fields
// land on PlayerProfile (class, evolution stage, lifetime sangue,
// keepers felled, etc.), this function grows to copy them in.
void loadActiveCharacterIntoPlayer(const selva::PlayerProfile& profile);

// Symmetric write-back: copy the player Actor's persistent fields out
// to the given PlayerProfile. Called by the pause-menu Save action and
// by Quit-to-Main-Menu before SaveManager writes to disk, so the saved
// file reflects what was earned in the run. v1 has nothing to write
// (PlayerProfile only carries `name`, which doesn't change at runtime).
void saveActiveCharacterFromPlayer(selva::PlayerProfile& profile);

// Render-world entry point. Frame capture readback lives here too
// (uses tickstate::frameCapture* flags).
void selvaRenderWorld(::Engine& engine, ::EntityManager& em, float camX, float camY, float alpha);

} // namespace selva::gameplay
