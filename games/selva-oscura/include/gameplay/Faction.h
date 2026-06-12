#pragma once

#include <cstdint>
#include <string>

namespace selva::gameplay
{

// Cosmological form of an actor. Five canonical values per the
// bestiary doctrine + the "what kind of thing is this" axis the
// engine uses to gate combat-permission rules (orthogonal to
// Faction, which is "who is on whose side"). Locked per
// [[soul-animal-form-combat-doctrine]] +
// [[guide-pawn-doctrine-2026-06-01]] + docs/design/bestiary.md
// *Soul-form vs animal-form*.
//
//   UnjudgedSoul -- refused Hell's measurement at the gates. Lives
//                   in the selva oscura. Shape without substrate.
//                   Fragile (low HP/poise). Cannot inflict damage on
//                   other UnjudgedSouls (the Wood's soul-on-soul
//                   protocol). Examples: the Vagrant, the Guide.
//   DamnedSoul   -- judged souls in Hell, deformed by contrapasso.
//                   figura umana variants. Mid HP/poise. Sin-defined
//                   attack patterns. Examples: every limbo_shade,
//                   every Hell-resident enemy.
//   Animal       -- real biological animals + allegorical forces
//                   given physical form. Non-humanoid skeletons. High
//                   HP/poise/raw damage. Combat unrestricted. Examples:
//                   Lupa, classical guardians (Cerberus, Minotaur,
//                   etc.), leak-evolved Selva-organisms.
//   HellMachinery-- a soul promoted by Beatrice into a keeper-
//                   function. Body transformed, contrapasso machinery
//                   installed. "Law-of-the-circle rather than a soul-
//                   of-the-circle" (per setting.md). No longer a soul.
//                   Legendary HP/poise. Examples: the 9 current
//                   keepers, contrapasso-husks (transitional
//                   remnants). NOT YET SHIPPED -- reserved.
//   Divine       -- god-tier. Divine substrate (sangue-saturable;
//                   corruptible by Hell-substance). Boss-class.
//                   "Fragmentary, rabid" combat per canon. Examples:
//                   Beatrice (on REFUSAL + PURITY paths). NOT YET
//                   SHIPPED -- reserved.
enum class Form : std::uint8_t
{
    UnjudgedSoul = 0,
    DamnedSoul = 1,
    Animal = 2,
    HellMachinery = 3,
    Divine = 4,
};

const char* formName(Form f);
Form parseForm(const std::string& s);

// Who an actor is hostile/friendly to. Damage application checks
// faction pairs to decide if a hit applies. Small enum -- widen to a
// hostility matrix only when the game demands it.
//
// Note on Allied: actors with Allied faction (the Guide, future
// companions) can be damaged by Hostile actors but NOT by Player.
// Allied actors that ATTACK route through factionsHostile() with
// attacker=Allied; the rule grants damage against Hostile so an
// Allied NPC can fight enemies alongside the player.
//
// Note on Neutral: actors who don't attack first but CAN be attacked
// by Player (Souls/ER convention -- friendly NPCs the player can
// aggro into hostility). Same damage rule as Hostile from Player's
// perspective; the NPC's response to taking damage is governed by
// gameplay code (faction flip Allied->Hostile or Neutral->Hostile),
// not by the static factionsHostile() rule.
enum class Faction
{
    Player,  // the PC; hostile to Hostile-faction actors
    Hostile, // damned souls, demons; hostile to Player + Allied
    Neutral, // can be attacked but doesn't attack first
    Allied,  // friendly to Player (the Guide; future companions)
};

// Returns true if `attacker` should damage `target` on hit.
constexpr bool factionsHostile(Faction attacker, Faction target)
{
    // Player damages Hostile + Neutral (Souls-style aggro of friendlies).
    // Player does NOT damage Allied directly through this rule -- a
    // separate "attack an ally" code path may convert Allied->Hostile
    // first then re-evaluate.
    if (attacker == Faction::Player && (target == Faction::Hostile || target == Faction::Neutral))
        return true;
    // Hostile damages Player + Allied.
    if (attacker == Faction::Hostile && (target == Faction::Player || target == Faction::Allied))
        return true;
    // Allied damages Hostile (the Guide swinging at Lupa). Allied does
    // NOT damage Neutral or Player. Friendly-fire on the player is
    // explicitly NOT a feature in v1.
    if (attacker == Faction::Allied && target == Faction::Hostile)
        return true;
    return false;
}

// Parse "Player" / "Hostile" / "Neutral" / "Allied" -> Faction.
// Unknown / empty defaults to Hostile (matches the legacy hardcoded
// default in spawnEnemyFromDecl, so omitting `faction` from existing
// enemy archetype JSON keeps shades hostile).
Faction parseFaction(const std::string& s);

// Inverse of parseFaction. Used by archetype to_json so JSON
// round-trips preserve the value.
const char* factionName(Faction f);

} // namespace selva::gameplay
