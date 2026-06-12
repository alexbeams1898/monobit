#pragma once

// Boss HUD: minimal on-screen overlay during a boss encounter --
// name display + HP bar + the "boss-felled" overlay on death.
//
// Polls selva::gameState().active_boss_idx / active_boss_id to know
// when to fade in / out. Reads the boss actor's hp.current/hp.max
// directly from the pool each frame. Reads boss_name + felled_message
// from the boss's archetype. No game logic here -- pure presentation.
//
// Per docs/design/ideas/boss_backend.md sections 8 + 9 + 11
// (impl plan steps 8 + 9).

namespace selva::ui
{

// Render the boss HUD overlay this frame. Call from the
// selva::ui::selvaRenderImGui hook (after TuningPanel; HUD draws
// over everything else). No-op when no boss is engaged AND the
// felled-overlay is not in progress.
void renderBossHud();

// Drop all cached HUD state -- snaps back to Hidden, clears
// cached_boss_id / cached_boss_name / etc. Call on phase transitions
// (main-menu -> game) so a stale Active mode from a previous session
// doesn't carry over and flash an empty HP bar / phantom "FELLED"
// overlay on load. Per-session UI state, not per-actor.
void resetBossHud();

} // namespace selva::ui
