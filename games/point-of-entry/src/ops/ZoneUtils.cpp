#include "ops/ZoneUtils.h"

#include "ecs/EntityManager.h"
#include "ecs/GameComponents.h"
#include "systems/DescentSystem.h"
#include "systems/WaveSystem.h"

#include <algorithm>

namespace zone
{
namespace
{
bool sCombat = false;
float sSettle = 1.0f;

// Long enough to read as a change rather than a flicker, short enough that a
// man walking through a door is not waiting on his own kit.
constexpr float kSettle = 0.30f;

bool resolve(const EntityManager& em)
{
    // Anything still on its feet, wherever he is: a floor is not finished while
    // the last of it is still walking, and neither is a room a leak filled.
    if (swarm::remaining(em) > 0)
        return true;
    // A hole this floor has not finished pressing -- the quiet between waves is
    // still the job.
    if (descent::roomHasWork())
        return true;
    // A passage with something coming THROUGH it -- the floors he left unfinished, reaching him
    // here. A quiet passage is furniture, and the weapon stays stowed beside it.
    for (const auto [e, site] : em.registry().view<const PlacedHole>().each())
        if (site.in_use)
            return true;
    return false;
}
} // namespace

void update(const EntityManager& em, float dt, bool cut)
{
    if (const bool now = resolve(em); now != sCombat)
    {
        sCombat = now;
        sSettle = 0.0f;
    }
    // A changeover IN PROGRESS is held at its start while the screen is black, so it is spent
    // on the room he arrives in rather than behind the curtain. One that has already finished
    // is left alone: every room swap is a cut, and winding a settled state back to zero
    // manufactured a changeover out of nothing -- which is the kit flashing stowed and back on
    // a walk between two rooms where nothing about the work changed at all.
    if (cut && sSettle < 1.0f)
        sSettle = 0.0f;
    else if (sSettle < 1.0f)
        sSettle = std::min(1.0f, sSettle + dt / kSettle);
}

float settle()
{
    return sSettle;
}

bool combat()
{
    return sCombat;
}

void reset()
{
    sCombat = false;
    sSettle = 1.0f;
}

} // namespace zone
