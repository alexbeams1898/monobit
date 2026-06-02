#include "gameplay/BossRewards.h"

#include "combat/CombatLog.h"
#include "gameplay/Actor.h"

namespace selva::gameplay
{

void onBossFelled(const std::string& boss_id, Actor& /*killer*/)
{
    // Per-boss reward dispatch. v1: imperative code, one case per
    // shipped boss. Becomes data-driven (archetype fields) when
    // enough bosses ship to settle on a generic shape.
    //
    // Per docs/design/ideas/boss_backend.md section 12.
    selva::combat::combatLog("[boss-reward] onBossFelled('{}') -- dispatch", boss_id);

    if (boss_id == "lupa")
    {
        // TBD by Alex: what does Lupa drop?
        // - Not sangue (she's not a demon; sangue is Hell-substance
        //   only per [[selva-wood-lore-locked-2026-05-31]]).
        // - Candidates: an item / offering / Grimoire-unlock /
        //   world-state flip (e.g. wood begins to heal).
        // - Stubbed for now -- the wiring is in place; content lands
        //   in a follow-up commit.
        selva::combat::combatLog(
            "[boss-reward] lupa: no reward content yet (TBD per design)");
        return;
    }

    selva::combat::combatLog("[boss-reward] no reward case for boss '{}'", boss_id);
}

} // namespace selva::gameplay
