#include "loot/StarterDispenser.h"

#include "AppState.h"
#include "AppStateGlobal.h"
#include "ecs/Items.h"
#include "loot/Pickups.h"

#include <glm/vec3.hpp>

#include <cstdio>
#include <string>

namespace selva::loot
{

namespace
{

constexpr const char* kGrantedFlag = "descent_starter_granted";
constexpr const char* kSigningFlag = "signing_committed";

// Live tracking: id of the currently-spawned starter pickup (so a
// repeated tick after spawn doesn't double-spawn). Reset to invalid
// on grant (the spawn callback sets the flag, gating future ticks)
// and on hardReset.
Id& starterIdSlot()
{
    static Id slot = kInvalidId;
    return slot;
}

// Class -> config_path mapping. Lives in code rather than JSON
// because class is engine-known enum + each path is a stable file
// reference; a config layer would just add indirection.
const char* starterConfigPathFor(PlayerClass cls)
{
    switch (cls)
    {
    case PlayerClass::Penitent:
        return "config/items/weapons/lead_mace.json";
    case PlayerClass::Heretic:
        return "config/items/weapons/bone_knife.json";
    case PlayerClass::Ferine:
        return "config/items/weapons/fang.json";
    case PlayerClass::Unburdened:
        return "config/items/invocations/ampoule.json";
    case PlayerClass::None:
    default:
        return nullptr;
    }
}

// Placement on the chapel_descent_corridor OBB. Corridor runs from
// (0, 20.71, -213.15) at top to (0, -43.31, -353.15) at bottom (per
// chapel_interior/region.json). t=0.35 down: visible from the top
// landing, ~50m descent before reaching it -- the player walks down
// and finds it. World position is the OBB's BOTTOM face (ramp
// surface) at that parameter; the pickup sits on the ramp.
glm::vec3 descentPickupPos()
{
    const glm::vec3 top{0.0f, 20.71f, -213.15f};
    const glm::vec3 bottom{0.0f, -43.31f, -353.15f};
    const float t = 0.35f;
    return top + (bottom - top) * t + glm::vec3{0.0f, 0.4f, 0.0f};
}

} // namespace

void tickStarterDispenser()
{
    PlayerProfile* profile = activePlayerProfile();
    if (profile == nullptr)
        return;

    if (hasFlag(profile, kGrantedFlag))
        return;

    if (!hasFlag(profile, kSigningFlag))
        return;

    Id& slot = starterIdSlot();
    if (slot != kInvalidId)
        return;

    const char* config_path = starterConfigPathFor(profile->player_class);
    if (config_path == nullptr)
    {
        std::fprintf(stderr,
                     "[starter] signing_committed but player_class=None; "
                     "skipping descent-stair spawn\n");
        std::fflush(stderr);
        return;
    }

    engine::ecs::ItemInstance inst;
    inst.config_path = config_path;
    inst.quantity = 1;

    const glm::vec3 pos = descentPickupPos();
    const Id spawned =
        spawnPickup(pos, inst, std::string{}, []() {
            if (PlayerProfile* p = activePlayerProfile())
                setFlag(p, kGrantedFlag);
            starterIdSlot() = kInvalidId;
            std::fprintf(stderr, "[starter] granted; flag set, slot cleared\n");
            std::fflush(stderr);
        });

    if (spawned == kInvalidId)
    {
        std::fprintf(stderr, "[starter] spawn rejected for '%s'\n", config_path);
        std::fflush(stderr);
        return;
    }

    slot = spawned;
    std::fprintf(stderr, "[starter] spawned '%s' at (%.2f,%.2f,%.2f) for class=%d\n",
                 config_path, static_cast<double>(pos.x), static_cast<double>(pos.y),
                 static_cast<double>(pos.z), static_cast<int>(profile->player_class));
    std::fflush(stderr);
}

void hardResetStarterDispenser()
{
    starterIdSlot() = kInvalidId;
}

} // namespace selva::loot
