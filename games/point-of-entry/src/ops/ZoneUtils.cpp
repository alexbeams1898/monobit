#include "ops/ZoneUtils.h"

#include "ecs/EntityManager.h"
#include "ecs/GameComponents.h"
#include "systems/TravelSystem.h"

namespace zone
{

bool combat(const EntityManager& em)
{
    // Generated space -- the dig itself -- is always the trade's ground.
    if (travel::currentArea().empty())
        return true;
    // Only an OPEN hole lets vermin reach him -- a sealed dig site is
    // furniture, and the weapon stays stowed beside it.
    for (const auto [e, site] : em.registry().view<const DigSite>().each())
        if (site.open && !site.trickle.empty())
            return true;
    return false;
}

} // namespace zone
