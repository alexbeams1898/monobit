#pragma once

#include "AppState.h"

// Beat 4 class-picker modal: the Signing UI. Full-screen modal,
// four options (Penitent / Heretic / Ferine / Refuse), two-step
// confirm, no back button. Per setting.md *The Signing and the
// commit-fire* + locked [[project_crucible_censer_leveling_system]].
//
// The Guide's dialog ends with this modal firing. The modal's
// committed selection writes to the active profile's player_class
// and dismisses; the player resumes control in the world with the
// commit-fire installed (Crucible for class-pickers, Censer for the
// unburdened).
//
// The modal is single-instance and global -- only one Signing happens
// per character (later Erasures re-open it via the same surface but
// pre-fill the current class for the player to swap).

namespace selva::ui
{

// True when the modal is currently displayed. Game tick suppresses
// world input while this is true (mirrors the existing scene-active
// pattern).
bool classPickerActive();

// Open the modal. Optional initial selection used by Erasure (where
// the player already has a class and is swapping); pass None for the
// fresh Beat 4 case.
void openClassPicker(PlayerClass initial_selection = PlayerClass::None);

// Render the modal if active. Drives selection + two-step confirm +
// commit-to-profile internally. No-op when inactive.
void renderClassPicker();

} // namespace selva::ui
