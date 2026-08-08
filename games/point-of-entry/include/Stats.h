#pragma once

#include <string>

namespace entt
{
enum class entity : unsigned int;
}
class EntityManager;

// The exterminator's sheet: five stats, everything else derived.
//
// Three are DISCIPLINES that weapons scale off (a tool's config carries per-stat scaling
// grades), one is the body, one finds things. There is deliberately no authored health, no
// authored stamina, and no authored defense anywhere in the game -- all of it derives from
// these five through the formulas in config/stats.json, so a number can never disagree with
// the stats that should explain it.
//
// LEVEL IS DERIVED TOO: it is the number of points spent above baseline, nothing more. There
// is no second progression number to keep in sync with the first.
struct Stats
{
    int chemical = 1;   // spray and tank tools
    int physical = 1;   // struck, trapped and heat tools; feeds a little health
    int biological = 1; // organism tools
    int endurance = 1;  // the body: health and stamina
    int inspection = 1; // drops, money, what gets noticed
};

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

} // namespace stats
