#include "gameplay/BossRewards.h"

#include "combat/CombatLog.h"
#include "gameplay/Actor.h"

namespace selva::gameplay
{

// Per-boss reward dispatch. Imperative today; goes data-driven once
// enough bosses ship to settle the shape. See ideas/boss_backend.md §12.
void onBossFelled(const std::string& boss_id, Actor& /*killer*/)
{
    selva::combat::combatLog("[boss-reward] onBossFelled('{}') -- dispatch", boss_id);
    if (boss_id == "lupa")
    {
        selva::combat::combatLog("[boss-reward] lupa: no reward content yet (TBD per design)");
        return;
    }
    selva::combat::combatLog("[boss-reward] no reward case for boss '{}'", boss_id);
}

} // namespace selva::gameplay
