#include "AppStateGlobal.h"

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

} // namespace selva
