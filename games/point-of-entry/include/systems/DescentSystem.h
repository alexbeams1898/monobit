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
    int node = -1;
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
    std::string kind; // a seep file; kept because a floor across the passage rebuilds this
                      // hole's program without building the floor it belongs to
    bool opened = false;
    bool cleared = false;
    int killed = 0; // how much of its program he has taken, so it resumes rather than restarts
};

// WHAT A FLOOR IS once the world it built is gone -- everything needed to make it again, and
// nothing that can be derived. Its space (an authored level's name, or a seed), where it sits,
// and its holes. Art and leaks rebuild from the space, so they are absent on purpose: this
// struct IS the save's shape for the descent, and a field that does not persist must not be
// able to appear in it.
struct Floor
{
    std::string area; // an authored level, or empty for generated space
    // THE FLOOR'S TAG, as it appears on every surface that names it: B<depth>-<room>, where the
    // room letter runs A..Z then AA, AB the way spreadsheet columns do -- unbounded, and never
    // a digit, which would make the boundary with the depth unreadable. Written once when the
    // floor is first dug and never recomputed: a tag that changed when a neighbour was dug is a
    // tag nobody can rely on, and the whole point of numbering a thing is that it is permanent.
    std::string label;
    // WHAT KIND OF SPACE THIS IS (config/floors/*.json): its shape, its look, the holes it can
    // grow. Decided by the hole that opened it -- a gnawed gap opens a warren -- and kept,
    // because the floor rebuilds from it on every visit and a retuned table must not turn a
    // room he has stood in into a different room.
    std::string type;
    unsigned seed = 0;
    int depth = 0;
    // HOW MANY HOLES IN WALLS HE HAS COME THROUGH since the last one in a floor. Zero on any
    // floor arrived at by going down. A type stops growing wall holes past its own limit, so a
    // run sideways ends by construction rather than by a counter refusing a hole that already
    // looks like a passage.
    int hops = 0;
    // WHICH HOLE HE CAME IN BY, or -1 on the first floor, which has nothing above it. Always 0
    // where it exists -- the way in is placed before the floor's own holes -- but stored rather
    // than assumed, because a floor that is authored AND dug into would break the assumption
    // silently. Rewritten on every arrival: at an act boundary several holes lead into one
    // floor, so the way back is whichever way he actually came.
    int way_in = -1;
    std::vector<Hole> holes;
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

// TRAVEL A PASSAGE: dig (or re-enter) the floor beyond this floor's `hole`. The way he came in
// is one of these, so climbing back out is the same call on a different hole. Only a SPENT hole
// is a passage; anything else refuses loudly, as does one that is currently delivering.
bool travel(Engine& engine, EntityManager& em, int hole);

// Does this hole go DOWN a floor, or across at the same depth? Read from its kind's placement:
// a hole in the ground is a way underneath, a hole in a wall is a run through a cavity.
bool descends(int node, int hole);

// Watch the current floor: a hole whose assault exhausts becomes a passage.
void update(Engine& engine, EntityManager& em, float dt);

// Set every passage's leak from the tree: a passage leaks while the floor BEYOND it has
// something running -- a hole he broke open and did not finish -- and is quiet otherwise,
// including when nothing has been dug there at all. Runs whether or not he is in the dig,
// because the basement's hole is a passage like any other. Called by update.
void refreshLeaks(EntityManager& em);

// IS THIS DEPTH AN ACT BOUNDARY -- one floor that every branch DESCENDING into it leads to?
// The descent branches within an act and converges at its end, which is what makes it a delta
// narrowing onto one root rather than a tree that only ever widens. Asked of the descending
// edge and never of the depth alone: a room reached sideways sits at the act's depth without
// being an arrival into the act, and collapsing those would delete lateral rooms there.
bool convergesAt(int depth);

// The tag of a floor, and of one of its points of entry (B2-A-01). Holes are numbered from one
// in the order the floor lists them, because a number on a wall is not an index.
std::string floorLabel(int node);
std::string poeTag(int node, int hole);

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
bool floorHasWork();

} // namespace descent
