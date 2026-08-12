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
    if (descent::floorHasWork())
        return true;
    // A way down with something coming UP it -- the floors he left unfinished, reaching him
    // here. A quiet passage is furniture, and the weapon stays stowed beside it.
    for (const auto [e, site] : em.registry().view<const DigSite>().each())
        if (site.leaking)
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
    // Held at the start of the changeover while the screen is black, so the
    // crossover is spent on the room he arrives in rather than the curtain.
    if (cut)
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
