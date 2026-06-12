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
    case PlayerClass::Wretched:
        return "Wretched";
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
    if (name == "Wretched")
        return PlayerClass::Wretched;
    if (name == "Unburdened")
        return PlayerClass::Unburdened;
    return PlayerClass::None;
}

bool isClassPickerPath(PlayerClass c)
{
    // Only the three signed-class identities carry the chrism-fire
    // (Crucible verb). Unburdened carries the channel-fire (Censer verb).
    // None has no commit verb (pre-Beat-4).
    return c == PlayerClass::Penitent || c == PlayerClass::Heretic || c == PlayerClass::Wretched;
}

} // namespace selva
