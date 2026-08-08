#pragma once

#include <vector>

#include <entt/entt.hpp>

// Every component this game adds to the engine's. One header, like the reference: a
// component is a noun the whole game may need, and nouns scattered across system headers
// are how two systems end up defining the same idea twice.
//
// The things a fight is made of. Game-side rather than engine-side: how a body takes damage and
// how effort is spent are this game's rules, and another game on this engine would want its own.

// Health is the ENGINE's (ecs/Components.h) and integer-valued -- a body's hit points are not
// this game's invention, and whole numbers keep damage legible.

// Effort. Spending resets recovery_timer; regen begins only once that has run down, so
// firing continuously never recovers and the decision is when to stop rather than how fast the
// number refills.
struct Stamina
{
    float current = 0.0f;
    float max_stamina = 0.0f;
    float recovery_timer = 0.0f;
};

// The tank on his back. Unlike stamina this does not come back on its own -- see ChargeTuning.
struct Charge
{
    float current = 0.0f;
    float max_charge = 0.0f;
};

// Just been hit. Drives a brief white flash; removed when it runs out.
struct HitFlash
{
    float remaining = 0.0f;
};

// Dying, but not yet gone. Death is a moment rather than an instant: destroying a thing on the
// frame its health runs out means the killing blow is the ONE hit that never flashes, which
// reads as the last hit being the weakest. Held here until the death flash has played.
struct Dying
{
    float remaining = 0.0f;
};

// Time until this creature can hurt the player by touching him again. Per-creature, so a crowd
// does not drain a bar the instant it closes.
struct TouchCooldown
{
    float remaining = 0.0f;
};

// How a creature walks WRONG. Insects do not move on smooth curves: they commit to a slightly
// mistaken heading, hold it a moment, correct, and pause. Held per creature rather than rolled
// per frame -- noise that changes every frame reads as a rendering fault, where an error that
// persists for a fifth of a second reads as a thing making bad decisions.
struct Skitter
{
    float wrong_angle = 0.0f; // radians of heading error currently being committed to
    float until = 0.0f;       // time left holding this error
    float pause_until = 0.0f; // time left standing still
    float phase = 0.0f;       // per-creature offset so a swarm never steps in unison
};

// How far a body has walked, for the code-driven gait. Distance rather than time, so the rock
// belongs to the movement instead of running on its own clock.
struct Gait
{
    float travelled = 0.0f;
    float rest = 0.0f; // 0 walking, 1 fully settled -- eases the hop out instead of freezing it
};

// What killing has paid, CARRIED. Not a bar that fills toward anything -- a pocketed sum that
// buys stat points at a rest spot, at a price that climbs with the level already bought. The
// price curve needs no ledger of its own: level derives from the sheet, so the sheet IS the
// receipt for everything ever spent.
struct Earnings
{
    int banked = 0;
};

// Somewhere he can put the tank down. Standing inside the radius and resting heals him and is
// the only place the sheet sells points -- the walk back with a full pocket is the tension.
struct RestSpot
{
    float radius = 40.0f;
};

// What a creature pays when it dies. On the creature, not in a table here -- the config that
// spawns it says what it is worth.
struct Worth
{
    int xp = 0;
};

// Just surfaced: an outward burst that overrides the hunt for a moment. Each creature erupts
// from the hole in its own direction, so a spray held at the pit mouth meets a scattering ring
// rather than a queue walking into the cone single-file.
struct Surge
{
    float dx = 0.0f;
    float dy = 0.0f;
    float speed = 150.0f; // set from config at emergence -- the component carries its tuning
    float remaining = 0.0f;
};

// Marks the vermin. Areas hurt these; the exterminator is not one.
struct Vermin
{
    float contact_damage = 0.0f;
};

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

// An area that hurts what is inside it.
//
// Every attack in the game is one of these, whether it appeared beside the player or travelled
// there. It hits EVERYTHING it overlaps, once each -- the already-hit list is the difference
// between this and a melee swing that stops at the first target, and it is what lets one
// trigger pull clear a crowd.
//
// One-shot areas live a single tick; lingering ones (a cloud, a puddle) persist and keep
// catching things that walk in, which is why the list is a set of who has been hit rather than
// a flag saying whether anything was.
struct HitArea
{
    float radius = 0.0f;
    // Cone half-angle in degrees, measured off dir. 0 means the whole circle -- a puff rather
    // than a sweep. A cone is what a swept wand actually covers, and it is the shape that makes
    // facing matter.
    float arc = 0.0f;
    // How fast the area's reach sweeps outward from its origin, px/s. Zero means the full
    // radius applies the instant it exists. A sprayed cone is chemical TRAVELLING -- if the far
    // edge kills before anything visibly arrives there, the picture and the rule disagree about
    // time, and the rule feels like a cheat even when it is generous.
    float expand = 0.0f;
    // A LINGERING area re-hits what stands in it on this interval rather than once, or a held
    // stream would tickle each ant a single time and then do nothing while pointed at it.
    float rehit = 0.0f;
    float damage = 0.0f;
    float remaining = 0.0f; // seconds left alive; expires at or below zero
    entt::entity owner = entt::null;

    // Travel, for a thrown area. Zero speed is one that simply sits where it appeared.
    float dir_x = 0.0f;
    float dir_y = 0.0f;
    float speed = 0.0f;
    float range_left = 0.0f;

    // Everything already hurt by this area. Small by construction -- an area lives for a moment
    // and touches what is in reach, not the whole floor.
    std::vector<entt::entity> hit;
    // When each of those may be hurt again, for a re-hitting area. Parallel to `hit`.
    std::vector<float> hit_at;
    float age = 0.0f;
};
