#pragma once

#include <string>
#include <vector>

#include <entt/fwd.hpp>

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
// A hole whose program exhausts flips from spawner to a WAY DOWN and starts
// carrying whatever he left running below it. Descending opens (or re-enters) the
// child floor at depth+1;
// every floor's way in carries the way back up. Death touches none of this --
// the tree survives everything but leaving the job.
namespace descent
{

// WHAT A FLOOR IS once the world it built is gone -- everything needed to make
// it again, and nothing that can be derived. Its space (an authored level's
// name, or a seed), where it sits in the tree, and which of its holes are
// spent. Holes, art and leaks all rebuild from the space, so they are absent
// here on purpose: this struct IS the save's shape for the descent, so a field
// that does not persist must not be able to appear in it.
struct Floor
{
    std::string area; // an authored level, or empty for generated space
    // THE FLOOR'S TAG, as it appears on every surface that names it: B<depth><room>, where the
    // room letter runs A..Z then AA, AB the way spreadsheet columns do -- unbounded, and never
    // a digit, which would make the boundary with the depth unreadable. Written once when the
    // floor is first dug and never recomputed: a tag that changed when a neighbour was dug is
    // a tag nobody can rely on, and the whole point of numbering a thing is that its number is
    // permanent.
    std::string label;
    unsigned seed = 0;
    int depth = 0;
    // THE WAY HE LAST CAME IN, which is not the same as where the floor came from: at an act
    // boundary several holes lead into ONE floor, so a floor has many ways in and only one of
    // them is the way back. Rewritten on every arrival, because the way out is whichever way
    // he came -- a field that recorded only the first would send him somewhere he never was.
    int from = -1;
    int from_hole = -1;
    std::vector<int> child;    // per hole: node index, -1 = never dug
    std::vector<bool> cleared; // per hole: assault spent?
    std::vector<bool> opened;  // per hole: has he broken it open? a sealed hole sends nothing
    // Per hole: which KIND of hole it is (a seep file). Kept rather than re-derived because a
    // floor ABOVE has to rebuild this hole's program -- its waves, its fauna, its pacing --
    // without building the floor it belongs to.
    std::vector<std::string> kind;
    std::vector<int> killed; // per hole: how much of its program he has taken
};

// The whole tree, and where in it he stands (-1 = nowhere). What a save keeps.
std::vector<Floor> snapshot();
int standing();

// Put a remembered tree back, then walk into `standing` -- the floor rebuilds
// from its space and its unfinished holes re-muster, which is what makes
// resuming mid-dig the same act as arriving.
void restore(const std::vector<Floor>& floors);
bool stand(Engine& engine, EntityManager& em, int node);

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
bool descend(Engine& engine, EntityManager& em, int hole);

// Climb back out of the current floor -- to the parent floor's hole, or to
// the authored basement at the root.
bool ascend(Engine& engine, EntityManager& em);

// Watch the current floor: a hole whose assault exhausts flips into a dig
// site with a leak previewing what lies below.
void update(Engine& engine, EntityManager& em, float dt);

// Set every dig site's leak from the tree: a way down leaks while the floor
// BEHIND it has something running -- a hole he broke open and did not finish --
// and is quiet otherwise, including when nothing has been dug there at all.
// Runs whether or not he is in the dig, because the basement's hole is a way
// down like any other. Called by update.
void refreshLeaks(EntityManager& em);

// The hole he is standing on that could be broken open, or -1. Sealed holes only: an open one
// is a fight and a spent one is a way down.
// IS THIS DEPTH AN ACT BOUNDARY -- one floor that every branch above it leads into? The
// descent branches within an act and converges at its end, which is what makes it a delta
// narrowing onto one root rather than a tree that only ever widens.
bool convergesAt(int depth);

// The tag of a floor, and of one of its points of entry (B2A-03). The POE number is 1-based
// because it is a thing written on a wall, not an index.
std::string floorLabel(int node);
std::string poeTag(int node, int hole);

// Where he is standing, and where a way down or up would put him -- the tag the floor beyond
// it will carry, whether or not it has been dug yet.
std::string hereLabel();
std::string beyondLabel(int hole, bool downward);

// Which way a way leads, as a mark for the prompt: down a floor, up one, or across at the same
// depth. Shown instead of naming the act, because "descend" stops being true the moment a wall
// hole opens a room on the floor he is already on. +1 down, -1 up, 0 across.
int stepDir(int hole, bool downward);

// THE FLOOR'S EXCLUSION LIST: every point of entry on it and what it is doing. Sealed ones are
// on it too -- a hole he has not touched is work outstanding, and leaving it off the list would
// mean the only way to know a floor still has one is to walk the room looking.
//
// A point that is CARRYING counts as working: what comes up a way down arrives through that
// hole, so the hole is what he is fighting whatever floor's budget it spends.
enum class PointState
{
    Sealed,  // never broken open
    Working, // pressing, or carrying something from another floor
    Cleared  // spent, and nothing coming through it
};

struct Point
{
    std::string tag;
    PointState state = PointState::Sealed;
    int wave = 0;
    int waves = 0;
};
std::vector<Point> exclusions();

int openableUnderfoot(const EntityManager& em, float x, float y);

// Break one open: its assault begins, and its art stops pretending to be floor.
bool open(EntityManager& em, int hole);

// HOW MANY FRONTS he is holding: holes he broke open on this floor that are not yet spent,
// plus every passage currently carrying something up from below. One is the careful way to
// work. More than one is a choice, and what it buys is in the sheet's rate.
int frontsOpen();

// Does the floor he is standing in still have a hole that has not been spent?
// False in the authored world, on a floor whose every hole is done, and on one he has not
// broken open yet -- a floor nobody has disturbed is not work.
bool floorHasWork();

} // namespace descent
