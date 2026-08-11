#pragma once

#include <string>

class Engine;
class EntityManager;

// THE DESCENT IS A TREE, and it persists. Every dug floor is a node: a seed,
// a depth, and which of its holes have been cleared. A floor generates ONCE --
// re-entering rebuilds the identical layout from its stored seed -- so a
// cleared hole stays a way down and eight explored floors are places, not
// history. An UNFINISHED hole's assault re-musters on re-entry: the source
// keeps pressing until a hole is spent, and a spent hole is spent forever.
//
// A hole whose program exhausts flips from spawner to DIG SITE, leaking a
// preview of its vein. Digging opens (or re-enters) the child floor at
// depth+1; every floor's way in carries the way back up. Death touches none
// of this -- the tree survives everything but leaving the job.
namespace descent
{

// Forget the whole dig (leaving the job for the title).
void reset();

// He is no longer standing in the dig (the ride home) -- the tree survives.
void leave();

// Enter the tree's root from the authored world's dig site. Remembers which
// level to surface back into.
bool enterRoot(Engine& engine, EntityManager& em);

// Dig (or re-enter) the child behind the current floor's `hole`. Only a
// cleared hole digs; anything else refuses loudly.
bool dig(Engine& engine, EntityManager& em, int hole);

// Climb back out of the current floor -- to the parent floor's hole, or to
// the authored basement at the root.
bool ascend(Engine& engine, EntityManager& em);

// Watch the current floor: a hole whose assault exhausts flips into a dig
// site with a leak previewing what lies below.
void update(Engine& engine, EntityManager& em);

// Standing in a dug floor?
bool active();
int currentDepth();

// Has the root ever been opened? The authored basement's hole leaks only
// after the first descent.
bool rootOpened();

} // namespace descent
