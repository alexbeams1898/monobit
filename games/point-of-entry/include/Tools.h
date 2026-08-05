#pragma once

#include <string>
#include <vector>

class EntityManager;

// Extermination tools, and the areas they hit.
//
// A TOOL HITS AN AREA, NOT A TARGET. One trigger pull clears everything inside a shape rather
// than killing one thing, which is what makes a swarm playable: with a crowd on screen, one
// input per kill means the player can never keep up, and clearing a room stops being possible.
//
// Melee and ranged are the SAME primitive. The area is identical either way; only where it
// appears differs -- a close-range tool puts one beside the player in the aim direction, a
// ranged tool sends one travelling along it. There is deliberately no separate melee path.
namespace tools
{

enum class Reach
{
    // The area appears immediately, offset from the player along the aim direction. A sprayer,
    // a swung crowbar, a boot.
    Adjacent,
    // The area travels along the aim direction until it hits something or expires. A dart, a
    // canister, a lobbed trap.
    Thrown,
};

struct Tool
{
    std::string name;
    Reach reach = Reach::Adjacent;
    float radius = 0.0f; // of the area it hits, world px -- what upgrades most often change
    float offset = 0.0f; // how far from the player it lands (Adjacent) or starts (Thrown)
    float range = 0.0f;  // how far it travels before expiring (Thrown only)
    float speed = 0.0f;  // travel speed, world px/s (Thrown only)
    float damage = 0.0f;
    float cooldown = 0.0f; // seconds between shots -- the floor on rate of fire
    float stamina = 0.0f;  // spent per use; the real constraint on sustained firing
    float linger = 0.0f;   // how long the area persists; 0 = a single instant
};

// The bar itself, which belongs to the exterminator rather than to any one tool. Souls-shaped:
// spending resets a delay, and regen only begins once that delay has run out -- so firing
// continuously never recovers, and the decision is when to STOP.
struct StaminaTuning
{
    float max = 100.0f;
    float recovery_delay = 0.8f; // seconds of not spending before regen begins
    float recovery_rate = 45.0f; // per second, once it does
};

const StaminaTuning& stamina();

// What a use of this tool actually costs and does. Everything asks through these rather than
// reading the Tool's fields, because these numbers will eventually be derived from the
// exterminator's skills rather than authored flat -- and when that happens, the change belongs
// in one place instead of at every call site.
float damageOf(const Tool& tool);
float staminaOf(const Tool& tool);
float cooldownOf(const Tool& tool);
float radiusOf(const Tool& tool);

// Load the tools the player carries. Missing or malformed config is loud and leaves him
// carrying nothing, rather than silently inventing a weapon.
bool load(const std::string& path);

const std::vector<Tool>& all();

// Which tool is in hand.
int selected();
void select(int index);

// Tick cooldowns, and fire the held tool if it is aimed and ready. Areas are spawned here;
// resolving what they hit is the damage pass.
void update(EntityManager& em, float dt);

// Recover the bar. Separate from update() because recovery happens whether or not he is
// holding a tool, and must keep running while he is doing anything else.
void tickStamina(EntityManager& em, float dt);

} // namespace tools
