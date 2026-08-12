#pragma once

#include <string>

class Engine;
class EntityManager;

// THE DESCENT IS A TREE, and it persists. Every floor is a node: its space, a
// depth, and which of its holes have been cleared. A floor's space comes from
// an authored level or from a seed, and NOTHING else about it differs -- the
// basement he starts in is the first floor of the descent, its hole pressed
// and spent like any other, so the game has no special first case. A floor generates ONCE --
// re-entering rebuilds the identical layout from its stored seed -- so a
// cleared hole stays a way down and eight explored floors are places, not
// history. An UNFINISHED hole's assault re-musters on re-entry: the source
// keeps pressing until a hole is spent, and a spent hole is spent forever.
//
// A hole whose program exhausts flips from spawner to DIG SITE and starts
// leaking a preview of its vein -- straight away, because an undug floor is
// nothing but work. Digging opens (or re-enters) the child floor at depth+1;
// every floor's way in carries the way back up. Death touches none of this --
// the tree survives everything but leaving the job.
namespace descent
{

// How a way down behaves: how close he must stand to be offered it, and how
// often an unfinished floor sends one up through it. From the dig's config, so
// the reach of every hole in the game is one number in one file.
struct SiteFeel
{
    float reach = 18.0f;
    float leak_interval = 8.0f;
};
const SiteFeel& siteFeel();

// Forget the whole dig (leaving the job for the title).
void reset();

// He is no longer standing in the dig (the ride home) -- the tree survives.
void leave();

// Dig (or re-enter) the child behind the current floor's `hole`. Only a
// cleared hole digs; anything else refuses loudly.
bool dig(Engine& engine, EntityManager& em, int hole);

// Climb back out of the current floor -- to the parent floor's hole, or to
// the authored basement at the root.
bool ascend(Engine& engine, EntityManager& em);

// Watch the current floor: a hole whose assault exhausts flips into a dig
// site with a leak previewing what lies below.
void update(Engine& engine, EntityManager& em);

// Set every dig site's leak from the tree: a way down leaks while the floor
// BEHIND it still has work -- an unspent hole, or no floor dug there at all --
// and goes quiet the moment there is nothing left down there to send up. Runs
// whether or not he is in the dig, because the authored basement's hole is a
// way down like any other. Called by update.
void refreshLeaks(EntityManager& em);

// Does the floor he is standing in still have a hole that has not been spent?
// False in the authored world, and false on a floor whose every hole is done.
bool floorHasWork();

} // namespace descent
