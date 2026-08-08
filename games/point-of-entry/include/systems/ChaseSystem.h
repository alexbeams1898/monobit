#pragma once

class EntityManager;

// The junk walking at you.
//
// Vermin have no attacks and no decisions: they follow the engine's flow field toward the
// exterminator and hurt him by touching him. That is deliberate rather than cheap -- an enemy
// with an attack animation, a windup and a state machine cannot exist two hundred at a time, and
// two hundred at a time is the point. Variety comes from what a creature IS (speed, health,
// how it moves) rather than from what it decides.
namespace chase
{

// Must run AFTER FlowFieldSystem, which rebuilds the field this reads. Internally: decide
// velocities, let the engine's separation push neighbours apart, then move against walls --
// deciding and moving in one pass is why a swarm used to converge into a single column hidden
// behind the player's sprite.
void update(EntityManager& em, float dt);

} // namespace chase
