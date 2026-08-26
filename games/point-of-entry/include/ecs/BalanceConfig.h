#pragma once

#include "ecs/GameComponents.h"

#include <string>

namespace entt
{
enum class entity : unsigned int;
}
class EntityManager;

namespace stats
{

// Every tunable in the derivations, loaded from config/stats.json. Missing file leaves the
// defaults, loudly.
struct Formulas
{
    struct
    {
        float base = 80.0f;
        float endurance_scale = 12.0f;
        float physical_scale = 4.0f;
    } health;
    struct
    {
        float base = 90.0f;
        float endurance_scale = 8.0f;
        float recovery_delay = 0.8f;
        float recovery_rate = 45.0f;
    } stamina;
    struct
    {
        float level_scale = 0.4f;
        float physical_scale = 0.8f;
        float endurance_scale = 0.5f;
    } defense;
    struct
    {
        // How much one stat point moves a scaling weapon, per grade point. The grade in a
        // tool's config multiplies this.
        float per_point = 0.08f;
    } scaling;
    struct
    {
        // Guarding converts contact damage into stamina at this exchange rate; a bar too
        // empty to pay breaks the guard and the hit lands whole.
        float stamina_per_damage = 2.0f;
        // A guard too spent to pay still stands between the hit and the bar:
        // this fraction of the damage gets through, the rest is the raised
        // arm. Only dropping the guard entirely eats a hit whole.
        float broken_factor = 0.6f;
        // Walking while braced. A TOUCH slower, not a stance-tax: enough that raising the
        // guard has weight, far short of the half-speed trudge that made blocking feel like a
        // punishment for using it.
        float walk_factor = 0.85f;
    } block;
    struct
    {
        // WHAT HE HAS WHEN THE TANK IS DRY. Weak on purpose: this is how he backs out of a room,
        // not how he works one. Damage comes off PHYSICAL -- the body stat -- because a man
        // hitting something with his hand is the one attack in this game the equipment has
        // nothing to do with.
        float base = 2.0f;
        float per_physical = 0.8f;
        float reach = 26.0f; // shorter than anything he carries
        float arc = 70.0f;   // DEGREES off his facing, like every area: a swing, not a spray
        float cooldown = 0.42f;
        float stamina = 4.0f;
    } fists;
    struct
    {
        // What each front BEYOND THE FIRST adds to the rate. Holding one hole at a time is the
        // careful way and pays plainly.
        float bonus_per_extra = 0.35f;
    } fronts;
    struct
    {
        // Quality score cutoffs (0-50 roll + Inspection's nudge): below [0] crude, then
        // standard, then fine; past [2] superior.
        float quality_thresholds[3] = {20.0f, 38.0f, 48.0f};
        float inspection_chance_scale = 0.06f; // drop chance, multiplicative per point
        float inspection_quality_scale = 1.5f; // score points added per Inspection point
    } loot;
    struct
    {
        int xp_base = 40;        // price of the first point
        float xp_growth = 1.35f; // each point costs this much more
    } reward;
};

bool load(const std::string& path);

// The starting sheet, from the config's "player" block. All ones if absent.
const Stats& playerStart();
const Formulas& formulas();

// Points spent above the all-ones baseline. THE level -- there is no other.
int level(const Stats& s);

int maxHealth(const Stats& s);
float maxStamina(const Stats& s);

// Flat damage shaved off anything that hits him. Derived, never authored: level in an
// equation with the two stats a body is made of.
int defense(const Stats& s);

// Write the derived attributes (Health, Stamina) onto an entity from its Stats. Call after
// stats change -- levelling, or first spawn. Preserves current fractions where sensible.
void applyDerivations(EntityManager& em, entt::entity entity);

// What each front BEYOND THE FIRST adds to the rate the work pays at. Holding two holes at
// once is a deliberate risk, and this is what it buys.
float frontBonus();

} // namespace stats
