#include "Settings.h"

#include <cstring>

namespace settings
{

const char* visibilityName(hud::Visibility v)
{
    switch (v)
    {
    case hud::Visibility::On:
        return "on";
    case hud::Visibility::Off:
        return "off";
    case hud::Visibility::Auto:
        break;
    }
    return "auto";
}

hud::Visibility visibilityFromName(const char* name, hud::Visibility fallback)
{
    if (name == nullptr)
        return fallback;
    if (std::strcmp(name, "on") == 0)
        return hud::Visibility::On;
    if (std::strcmp(name, "off") == 0)
        return hud::Visibility::Off;
    if (std::strcmp(name, "auto") == 0)
        return hud::Visibility::Auto;
    return fallback; // an unknown word keeps what the caller had rather than guessing
}

} // namespace settings
