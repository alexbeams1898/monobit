#pragma once

#include "ecs/BalanceConfig.h"

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
    // Held down: the area persists at the aim direction for as long as the trigger is held and
    // the tank has charge, sweeping as the cursor moves. A sprayer, a fogger, a torch.
    Stream,
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
    float offset = 0.0f; // from the player: where it lands (Adjacent), starts (Thrown), or the
                         // nozzle sits (Stream)
    float range = 0.0f;  // how far it travels before expiring (Thrown only)
    float speed = 0.0f;  // travel speed, world px/s (Thrown only)
    float damage = 0.0f;
    float cooldown = 0.0f; // seconds between shots -- the floor on rate of fire
    float stamina = 0.0f;  // spent per use; his body's limit
    float charge = 0.0f;   // spent per use (or per second, streaming) -- the TANK's limit
    float arc = 0.0f;      // cone half-angle in degrees; 0 = the area is a full circle
    float linger = 0.0f;   // how long the area persists; 0 = a single instant
    // WHAT IT SOUNDS LIKE. A held trigger is THREE clips, not one: the pull, a body that
    // repeats, and the dribble after release. One clip cannot do it -- looped whole, its own
    // ramp-up and dying-away replay every pass and you hear it swell and fade; and a single
    // click cuts it to a fragment instead of a complete little burst.
    std::string sfx_start;
    std::string sfx_loop;
    std::string sfx_stop;
    std::string sfx_dry; // the cough when he pulls on an empty tank
    float sfx_volume = 1.0f;
    // Per-stat scaling grades: how much this tool rewards each discipline. Blended with the
    // holder's sheet by damageOf -- the same wand is a different weapon in different hands.
    float scale_chemical = 0.0f;
    float scale_physical = 0.0f;
    float scale_biological = 0.0f;
};

// THE TANK. What the wand runs on, and it does NOT refill by waiting -- only by working. Killing
// pest recovers charge, which keeps the player in the fight rather than sending him to a shop
// to buy ammunition, and quietly makes the fiction better: he is putting back what he takes out.
struct ChargeTuning
{
    float max = 100.0f;
    float per_kill = 6.0f; // recovered for each thing killed
};

const ChargeTuning& chargeTuning();

// Recover charge for a kill. Called where things die.
void creditKill(EntityManager& em);

// What a use of this tool actually costs and does, IN THESE HANDS. Everything asks through
// these rather than reading the Tool's fields -- the tool says what it is, the sheet says who
// is holding it, and only these functions know how the two blend.
float damageOf(const Tool& tool, const Stats& holder);
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

// Move to the next tool in the kit, wrapping. What the cycle key calls.
void next();

// Is a held stream live right now? Facing wants to know: a man spraying faces his work.
bool streaming(EntityManager& em);

// THE GUARD. Contact damage arriving while guarding is paid in stamina at the
// block exchange rate; a bar too empty to pay breaks the guard and the hit
// lands whole. Returns the damage that still applies to health.
int absorbWithGuard(EntityManager& em, int damage, bool guarding);

// Put the tool up. Ends a held stream unconditionally -- called wherever the
// world moves under him (death, travel, leaving a combat zone), because the
// stream's own end-of-trigger cleanup lives in update(), and update() does
// not run everywhere he can wake up.
void holster(EntityManager& em);

// Tick cooldowns, and fire the held tool if it is aimed and ready. Areas are spawned here;
// resolving what they hit is the damage pass.
void update(EntityManager& em, float dt);

// Recover the bar. Separate from update() because recovery happens whether or not he is
// holding a tool, and must keep running while he is doing anything else.
void tickStamina(EntityManager& em, float dt);

// Move and age the spray. Separate from update() because droplets outlive the trigger pull that
// made them -- releasing the button should not delete chemical already in the air.
void tickParticles(EntityManager& em, float dt);

} // namespace tools
