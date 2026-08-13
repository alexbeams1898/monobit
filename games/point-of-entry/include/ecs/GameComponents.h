#pragma once

#include "ecs/ItemConfig.h"

#include <string>
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
    // BRACED: 0 loose, 1 rigid. The goofy walk is a body that does not expect to be hit; a man
    // holding a guard up plants his feet and stops swinging. Written by whoever knows the body
    // is braced, so the gait itself stays a thing that only turns distance into motion.
    float braced = 0.0f;
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

// A hole the MAP places, naming its seep type. An authored room carrying one
// is a floor like any other: the hole presses its assault, goes spent, becomes
// a way down, and leaks -- the same life a generated floor's holes have, which
// is why the first one is in his own basement and not a special case.
struct AuthoredSeep
{
    std::string kind; // a seep file: what kind of hole, and what comes through
};

// A SPENT HOLE, which is to say A PASSAGE. Standing on it offers the way through, and taking
// it opens the floor on the other side -- the floor below through a hole in the ground, another
// room at the same depth through a hole in a wall. It also CARRIES: an unfinished hole anywhere
// beyond it is still pressing, and this is the mouth it reaches him through, one hole at a time
// out of that hole's own finite program. The way he came in wears this too; it is a hole that
// arrived already spent, and nothing about it is a special case.
struct PassageSite
{
    // Stand ON it to be offered the way through -- the prompt is the square underfoot, never
    // the neighbourhood. Filled from the dig's config at placement (descent::siteFeel).
    float radius = 0.0f;
    int hole = -1; // which of this floor's holes it is
    // IN USE. Did he leave something RUNNING on the other side? A floor he never dug, or dug
    // and never broke anything open on, sends nothing -- a sealed hole is sealed at every
    // depth. What comes through is what he disturbed and walked away from, so this is a report
    // on his own unfinished business. A passage carrying something is not a way anywhere.
    // Derived every frame by descent::refreshLeaks.
    bool leaking = false;
    // THE MOUTH -- never assumed from the entity's transform: a wall-mounted hole's art sits in
    // the wall, and what comes through a passage may never arrive somewhere other than where
    // the passage is.
    float spawn_x = 0.0f;
    float spawn_y = 0.0f;
};

// A placed hole's art, tagged with which of the floor's holes it draws. When
// the hole is spent this same entity becomes its dig site -- one sprite, one
// spot, nothing stacked to flicker.
// A hole's own art, and which of the floor's holes it is. It carries both of its faces so
// opening one is a frame swap rather than a reload: CLOSED is the outline with the cavity
// drawn out of it, so the floor shows through and a sealed hole matches whatever ground it
// sits in; OPEN is the hole itself.
struct SeepArt
{
    int hole = -1;
    int closed_x = 0; // pixel offset of the closed frame on the sheet
    int open_x = 0;
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

// How a species MOVES WRONG, declared in its creature file. All zeros -- the default -- is a
// plain straight walk: character is opt-in, and a creature that declares nothing gets nothing.
// buzz is a fast tremor drawn over the glide (insect language); drift is a slow lateral float
// on the approach so a crowd does not converge into one line.
struct Motion
{
    float buzz_hz = 0.0f;
    float buzz_amount = 0.0f; // world px, perpendicular to travel, draw-only
    float drift_hz = 0.0f;
    float drift_amount = 0.0f;
};

// What he is carrying. Instances stack by (item, quality) -- a fine flake and a crude flake
// are different goods and stay different stacks.
struct Satchel
{
    std::vector<ItemInstance> items;
};

// A thing lying where something died. Collected by WALKING ONTO it -- no magnet: currency is
// automatic because it is abstract, but goods are picked up by a man bending down, and the
// difference is the difference between income and work.
struct ItemDrop
{
    ItemInstance contents;
};

// The species' drop table, riding the creature so death does not need to know species exist.
struct DropTable
{
    std::vector<DropEntry> entries;
};

// Which hole this creature came out of, so each seep can gate its own next wave on ITS output
// being dead -- clearing gates progress, per hole, and two holes stagger honestly.
struct SeepSource
{
    int index = 0;
};

// Marks the vermin. Areas hurt these; the exterminator is not one. The numbers ride the
// component because they differ per SPECIES, and the systems that read them must not know
// species exist -- a creature is its config file, nowhere else.
struct Vermin
{
    float contact_damage = 0.0f;
    float speed = 46.0f;
};

// What kind of creature this body is, named by its creature file path -- the path IS the
// species identity everywhere (the swarm loads by it, evolution points at it, the record
// tallies by it), so one id names a species and no parallel enum can drift from the files.
struct Species
{
    std::string path;
};

// How strongly this individual smells of what is below, rolled once at emergence from a range
// that runs hotter with depth. The roll multiplied every derived stat and decided whether the
// evolved form surfaced; it stays on the body so anything later can read how hot this one ran.
struct Smell
{
    int amount = 0;
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

// Tags the live held-stream area -- on the entity, not in a system handle, so nothing dangles.
struct StreamHead
{
};

// One entry in an area's already-hurt ledger: who, and when it may be hurt again.
struct HitMark
{
    entt::entity target = entt::null;
    float next_at = 0.0f;
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

    // Everything already hurt by this area, and when each may be hurt again. Small by
    // construction -- an area lives for a moment and touches what is in reach, not the whole
    // floor.
    std::vector<HitMark> hit;
    float age = 0.0f;
};
