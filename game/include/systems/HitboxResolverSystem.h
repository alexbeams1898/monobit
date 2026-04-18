#pragma once

class EntityManager;

// HitboxResolverSystem -- each tick, for every entity with AttackLocked,
// reads the current animation frame + attacker's weapon hitbox shapes +
// attack-anim hitbox keyframes, transforms the shapes into world space, and
// tests them against nearby Hurtboxes. Emits CollisionEvent entries for
// DamageSystem to consume. Dedups per-swing via Hurtbox.hit_history so the
// same target isn't damaged twice by one swing's multi-frame active window.
//
// Runs AFTER AnimationSystem (so frame_index is current) and BEFORE
// DamageSystem (so it can consume the events).
namespace HitboxResolverSystem
{
void update(EntityManager& em, float dt);
}
