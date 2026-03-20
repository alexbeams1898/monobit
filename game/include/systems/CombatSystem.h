#pragma once

#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "ecs/GameComponents.h"
#include "ecs/GameConfig.h"

// ---------------------------------------------------------------------------
// CombatSystem — player attack input, weapon cooldown ticking, hitbox
// lifecycle, dodge roll, weapon skill, and auto-attack mode.
//
// Runs second in the update loop (after InputMappingSystem, before movement).
// This ensures hitboxes are spawned before CollisionSystem runs so hits
// register in the same frame the attack fires.
//
// Responsibilities:
//   1. Destroy hitbox entities left over from the previous frame.
//   2. Tick all combat timers (swing/skill/dodge cooldowns, AttackLocked,
//      Dodging, Staggered, Parrying) by dt.
//   3. On player attack input: spawn a Hitbox entity if cooldown allows.
//   4. On player dodge input: apply velocity impulse + grant i-frames.
//   5. On player skill input: spawn an enlarged Hitbox + reset skill cooldown.
//   6. In auto-attack mode: aim and fire at the nearest enemy automatically.
//   7. Toggle auto-attack mode when P is pressed (debug / dev shortcut).
//
// Pure logic — no rendering, no audio.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Combat formula helpers — exposed here so tests and DamageSystem can call
// them directly without going through the full system update.
// ---------------------------------------------------------------------------

// Compute swing cooldown from weapon physics + stat blend.
// Result is clamped to [0.05, ∞) seconds.
float computeSwingCooldown(const Weapon& w, const Stats& s, const FormulaConfig& f);

// Compute raw damage for one swing (before DEF and penalty are applied).
float computeDamage(const Weapon& w, const Stats& s, const FormulaConfig& f);

// Deduct stamina cost from an entity. Resets recovery delay.
// If the deduction depletes stamina to 0, emplaces Staggered (exhaustion stumble).
void deductStamina(entt::registry& reg, entt::entity entity, float cost, const FormulaConfig& f);

class CombatSystem
{
  public:
    static void update(EntityManager& em, double dt);
};
