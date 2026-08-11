#include "ops/ZoneUtils.h"

#include "ecs/EntityManager.h"
#include "ecs/GameComponents.h"
#include "systems/TravelSystem.h"

namespace zone
{

bool dug()
{
    return travel::currentArea().empty();
}

bool combat(const EntityManager& em)
{
    if (dug())
        return true;
    const auto view = em.registry().view<const DigSite>();
    return view.begin() != view.end();
}

} // namespace zone
