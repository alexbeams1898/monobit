#include "AppStateGlobal.h"
#include "gameplay/Actor.h"
#include "gameplay/Faction.h"
#include "items/InventoryOps.h"
#include "items/UseHandlers.h"

#include <cstdio>

namespace selva::items
{

// Registers the Seal's use-action + use-condition handlers under the
// keys referenced by config/items/seal.json. Called once at boot from
// main.cpp before the item registry loads.
void registerSealHandlers()
{
    registerUseAction("consume_seal", [](Inventory& inv, const std::string& item_id) {
        if (!remove(inv, item_id))
        {
            std::fprintf(stderr, "[seal] consume_seal called but '%s' not in inventory\n",
                         item_id.c_str());
            std::fflush(stderr);
            return;
        }
        selva::setFlag("seal_consumed");
        std::fprintf(stderr, "[seal] consumed; seal_consumed flag set\n");
        std::fflush(stderr);
    });

    registerUseCondition("unjudged_only", []() {
        const gameplay::Actor& pc = gameplay::player();
        if (pc.form != gameplay::Form::UnjudgedSoul)
            return UseGate{false, "Only the unjudged may break this Seal."};
        return UseGate{};
    });
}

} // namespace selva::items
