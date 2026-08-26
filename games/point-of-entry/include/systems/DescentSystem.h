#pragma once

#include "ops/NavUtils.h"

#include <string>
#include <vector>

#include <entt/fwd.hpp>

class Engine;
class EntityManager;

// THE DESCENT IS A TREE, and it persists. Every floor is a room: its space, a
// depth, and which of its holes have been cleared. A floor's space comes from
// an authored level or from a seed, and NOTHING else about it differs -- the
// basement he starts in is the first floor of the descent, its hole pressed
// and spent like any other, so the game has no special first case. A floor generates ONCE --
// re-entering rebuilds the identical layout from its stored seed -- so a
// cleared hole stays a way down and eight explored floors are places, not
// history. An UNFINISHED hole's assault re-musters on re-entry: the source
// keeps pressing until a hole is spent, and a spent hole is spent forever.
//
// A hole whose program exhausts flips from spawner to a PASSAGE and starts carrying whatever
// he left running on the other side of it. Travelling a passage opens (or re-enters) the floor
// beyond it -- one floor down through a hole in the ground, or another room at the SAME depth
// through a hole in a wall. Depth is therefore governed entirely by floor holes, which is what
// makes being locked into one kind of trail impossible by construction. Death touches none of
// this -- the tree survives everything but leaving the job.
namespace descent
{

// ONE END OF A PASSAGE. A hole names the floor on the far side AND which of that floor's holes
// it comes out at, so the connection is the same object read from either side -- there is no
// direction in it, and nothing has to look up a parent to find its way back.
struct Link
{
    int room = -1;
    int hole = -1;
};

// A POINT OF ENTRY. Its three states are the whole of what a floor tracks about it: SEALED is
// a question (neither opened nor cleared), OPEN is a fight, SPENT is a passage. The way he came
// IN is a hole like the rest -- it simply arrives already spent, which is what a passage is --
// so a floor has one kind of connection to everywhere and every rule is written once.
//
// A hole's KIND decides which way it goes: a wall-placed kind opens a room at the SAME depth, a
// floor-placed one opens the floor below. That is the ONLY thing direction changes.
struct Hole
{
    Link to;          // the far end, -1 until it has been dug
    std::string kind; // a hole file; kept because a floor across the passage rebuilds this
                      // hole's program without building the floor it belongs to
    // WHICH WALL IT IS CUT INTO. Meaningless for a hole in the ground, which faces up at him.
    //
    // A room's OWN holes derive this every build -- it is a fact about a map that comes back
    // identical from its seed, and a derived fact that is stored is one that goes on answering
    // by whatever rule was in force the day it was written down.
    //
    // What is stored is the WAY IN, which is the one hole whose side is not a fact about this
    // room at all: it is the reverse of the hole he came through, in a room that is not built
    // while he stands here. North on every hole dug before sides existed, which is what they
    // all were.
    world::Side side = world::Side::North;
    bool opened = false;
    bool cleared = false;
    int killed = 0; // how much of its program he has taken, so it resumes rather than restarts
};

// WHAT A FLOOR IS once the world it built is gone -- everything needed to make it again, and
// nothing that can be derived. Its space (an authored level's name, or a seed), where it sits,
// and its holes. Art and leaks rebuild from the space, so they are absent on purpose: this
// struct IS the save's shape for the descent, and a field that does not persist must not be
// able to appear in it.
struct Room
{
    std::string area; // an authored level, or empty for generated space
    // THE FLOOR'S TAG, as it appears on every surface that names it: B<depth>-<room>, where the
    // room letter runs A..Z then AA, AB the way spreadsheet columns do -- unbounded, and never
    // a digit, which would make the boundary with the depth unreadable. Written once when the
    // floor is first dug and never recomputed: a tag that changed when a neighbour was dug is a
    // tag nobody can rely on, and the whole point of numbering a thing is that it is permanent.
    std::string label;
    // WHAT KIND OF SPACE THIS IS (config/rooms/*.json): its shape, its look, the holes it can
    // grow. Decided by the hole that opened it -- a gnawed gap opens a warren -- and kept,
    // because the floor rebuilds from it on every visit and a retuned table must not turn a
    // room he has stood in into a different room.
    std::string type;
    unsigned seed = 0;
    int depth = 0;
    // WHICH HOLE HE CAME IN BY, or -1 on the first floor, which has nothing above it. Always 0
    // where it exists -- the way in is placed before the floor's own holes -- but stored rather
    // than assumed, because a floor that is authored AND dug into would break the assumption
    // silently. Rewritten on every arrival: at an act boundary several holes lead into one
    // floor, so the way back is whichever way he actually came.
    int way_in = -1;
    // CUT INTO A SLAB standing inside the room above, rather than through that room's edge --
    // and how big that slab was, in tiles. Zero for a room reached the ordinary way, the same
    // way an empty `area` means generated space.
    //
    // Two things follow from it and neither can be worked out later, because the room that knew
    // it is torn down the moment he steps through. It grows NO holes in walls: a slab has the
    // same room on both sides, so a passage through one would come out where it went in. And it
    // takes its shape from the slab, so what he walks into looks like the thing he walked into.
    int slab_cols = 0;
    int slab_rows = 0;
    std::vector<Hole> holes;
};

// The whole tree, and where in it he stands (-1 = nowhere). What a save keeps.
std::vector<Room> snapshot();
int standing();

// Put a remembered tree back, then walk into `standing` -- the floor rebuilds
// from its space and its unfinished holes re-muster, which is what makes
// resuming mid-dig the same act as arriving.
void restore(const std::vector<Room>& floors);
bool stand(Engine& engine, EntityManager& em, int room);

// HOW CLOSE HE STANDS to be offered the way through a passage. One number in one file, so the
// reach of every hole in the game is the same reach.
float holeReach();

// Forget the whole dig (leaving the job for the title).
void reset();

// He is no longer standing in the dig (the ride home) -- the tree survives.
void leave();

// TRAVEL A PASSAGE: dig (or re-enter) the floor beyond this floor's `hole`. The way he came in
// is one of these, so climbing back out is the same call on a different hole. Only a SPENT hole
// is a passage; anything else refuses loudly, as does one that is currently delivering.
bool travel(Engine& engine, EntityManager& em, int hole);

// Does this hole go DOWN a floor, or across at the same depth? Read from its kind's placement:
// a hole in the ground is a way underneath, a hole in a wall is a run through a cavity.
bool descends(int room, int hole);

// Watch the current floor: a hole whose assault exhausts becomes a passage.
void update(Engine& engine, EntityManager& em, float dt);

// Set every passage's leak from the tree: a passage leaks while the floor BEYOND it has
// something running -- a hole he broke open and did not finish -- and is quiet otherwise,
// including when nothing has been dug there at all. Runs whether or not he is in the dig,
// because the basement's hole is a passage like any other. Called by update.
void refreshPassages(EntityManager& em);

// IS THIS DEPTH AN ACT BOUNDARY -- one floor that every branch DESCENDING into it leads to?
// The descent branches within an act and converges at its end, which is what makes it a delta
// narrowing onto one root rather than a tree that only ever widens. Asked of the descending
// edge and never of the depth alone: a room reached sideways sits at the act's depth without
// being an arrival into the act, and collapsing those would delete lateral rooms there.
bool convergesAt(int depth);

// The tag of a floor, and of one of its points of entry (B2-A-01). Holes are numbered from one
// in the order the floor lists them, because a number on a wall is not an index.
std::string roomLabel(int room);
std::string poeTag(int room, int hole);

// Where he is standing, and where a passage would put him -- the tag the floor beyond it will
// carry, whether or not it has been dug yet.
std::string hereLabel();
std::string beyondLabel(int hole);

// Which way a passage leads, as a mark for the prompt: +1 deeper, -1 back up, 0 across at the
// same depth. A mark rather than a word, because no single verb stays true once a hole in a
// wall opens a room on the floor he is already standing on.
int stepDir(int hole);

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

// The hole he is standing on that could be broken open, or -1. SEALED holes only: an open one
// is already a fight and a spent one is already a passage.
int openableUnderfoot(const EntityManager& em, float x, float y);

// Break one open: its assault begins, and its art stops pretending to be floor.
bool open(EntityManager& em, int hole);

// HOW MANY FRONTS he is holding: holes he broke open on this floor that are not yet spent,
// plus every passage currently carrying something through. One is the careful way to work.
// More than one is a choice, and what it buys is in the sheet's rate.
int frontsOpen();

// Does the floor he is standing in still have a hole that has not been spent?
// False in the authored world, on a floor whose every hole is done, and on one he has not
// broken open yet -- a floor nobody has disturbed is not work.
bool roomHasWork();

} // namespace descent
