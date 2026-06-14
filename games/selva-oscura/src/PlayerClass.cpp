#include "AppState.h"

namespace selva
{

const char* playerClassName(PlayerClass c)
{
    switch (c)
    {
    case PlayerClass::Penitent:
        return "Penitent";
    case PlayerClass::Heretic:
        return "Heretic";
    case PlayerClass::Ferine:
        return "Ferine";
    case PlayerClass::Unburdened:
        return "Unburdened";
    case PlayerClass::None:
        break;
    }
    return "None";
}

PlayerClass parsePlayerClass(const std::string& name)
{
    if (name == "Penitent")
        return PlayerClass::Penitent;
    if (name == "Heretic")
        return PlayerClass::Heretic;
    // Ferine renamed from Wretched (locked 2026-06-14 per
    // [[project_class_stats_v2_locked_2026_06_14]]). Accept the legacy
    // "Wretched" string so saves from before the rename load with the
    // correct class identity. Both map to PlayerClass::Ferine; the next
    // save persists the new name.
    if (name == "Ferine" || name == "Wretched")
        return PlayerClass::Ferine;
    if (name == "Unburdened")
        return PlayerClass::Unburdened;
    return PlayerClass::None;
}

bool isClassPickerPath(PlayerClass c)
{
    // Only the three signed-class identities carry the chrism-fire
    // (Crucible verb). Unburdened carries the channel-fire (Censer verb).
    // None has no commit verb (pre-Beat-4).
    return c == PlayerClass::Penitent || c == PlayerClass::Heretic || c == PlayerClass::Ferine;
}

} // namespace selva
