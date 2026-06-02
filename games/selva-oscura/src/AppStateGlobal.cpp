#include "AppStateGlobal.h"

#include <algorithm>

namespace selva
{

GameState& gameState()
{
    static GameState s_gs;
    return s_gs;
}

SaveData& saveData()
{
    static SaveData s_sd;
    return s_sd;
}

UIState& uiState()
{
    static UIState s_ui;
    return s_ui;
}

Inventory& playerInventory()
{
    static Inventory s_inv;
    return s_inv;
}

Equipment& playerEquipment()
{
    static Equipment s_eq;
    return s_eq;
}

PlayerProfile* activePlayerProfile()
{
    const std::string& name = gameState().active_character;
    if (name.empty())
        return nullptr;
    for (auto& p : saveData().characters)
    {
        if (p.name == name)
            return &p;
    }
    return nullptr;
}

bool hasFlag(const PlayerProfile* profile, const std::string& flag)
{
    if (profile == nullptr || flag.empty())
        return false;
    return std::find(profile->flags.begin(), profile->flags.end(), flag) != profile->flags.end();
}

bool setFlag(PlayerProfile* profile, const std::string& flag)
{
    if (profile == nullptr || flag.empty())
        return false;
    if (std::find(profile->flags.begin(), profile->flags.end(), flag) != profile->flags.end())
        return false;
    profile->flags.push_back(flag);
    return true;
}

bool clearFlag(PlayerProfile* profile, const std::string& flag)
{
    if (profile == nullptr || flag.empty())
        return false;
    auto it = std::find(profile->flags.begin(), profile->flags.end(), flag);
    if (it == profile->flags.end())
        return false;
    profile->flags.erase(it);
    return true;
}

bool hasFlag(const std::string& flag)
{
    return hasFlag(activePlayerProfile(), flag);
}

bool setFlag(const std::string& flag)
{
    return setFlag(activePlayerProfile(), flag);
}

bool clearFlag(const std::string& flag)
{
    return clearFlag(activePlayerProfile(), flag);
}

} // namespace selva
