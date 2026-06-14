#pragma once

// Descent-stair starter weapon dispenser. Per classes.md *Class-shaped
// descent-stair starter weapons*: after the Signing commits at Beat 4,
// a class-shaped tier-0 weapon appears partway down the chapel descent
// staircase. The Vagrant walks down and picks it up -- DS1-Asylum-stair
// ode. One starter per character per cycle; once granted, the
// `descent_starter_granted` flag on the active PlayerProfile records
// the grant and prevents re-spawn for that character.
//
// Lives outside the generic Pickups infra (loot/Pickups.h) because the
// dispenser owns the trigger condition + per-character emptied-state
// flag; Pickups stays a passive runtime registry that doesn't know
// anything about flags or class. Wiring direction is one-way:
// dispenser -> spawnPickup; the granted callback then sets the flag.

namespace selva::loot
{

// Per-frame tick. Cheap when the gate is already satisfied (one flag
// check + early-return). Fires the spawn exactly once: when
// `signing_committed` is set AND `descent_starter_granted` is not. The
// spawned pickup's grantPickup callback (default behavior in
// Pickups.cpp) handles the inventory grant + toast; the dispenser
// observes the grant on the NEXT tick by re-checking the flag the
// pickup-grant path will set.
void tickStarterDispenser();

// Wipe dispenser state. Called from hardResetWorldForCharacter so a
// character switch starts the next descent fresh. Per-character
// emptied state lives on PlayerProfile.flags and survives this reset
// (so a returning character whose previous run granted the starter
// doesn't get a second one).
void hardResetStarterDispenser();

} // namespace selva::loot
