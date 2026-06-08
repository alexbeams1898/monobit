#pragma once

// Beat-3 name-prompt modal -- the Guide asks the Vagrant what to
// call him. Replaces the prior main-menu CharCreate name-entry per
// the locked design (story.md Beat 3: the Guide elicits the name in
// dialog, not on a setup screen).
//
// Pattern mirrors ui::ClassPickerScreen:
//   - openNamePrompt() opens the modal.
//   - renderNamePrompt() draws it + handles input (no-op when inactive).
//   - namePromptActive() lets other systems (mouse capture, HUD
//     suppression) check whether the modal owns the screen.
//
// Commit behavior: when the player confirms a valid 6-letter name,
// the modal writes the name into selva::pendingProfile() AND sets
// the gameplay flag "name_given" so dialog gating works. It does
// NOT yet persist a character to disk -- the SaveManager finalize
// happens at Signing-commit (in ClassPickerScreen::commitClass).
//
// Per the doctrine: each Vagrant starts fresh, the name is given
// not recovered, and "death before Signing is by design impossible"
// (the wood-segment HP-floor invariant).

namespace selva::ui
{

bool namePromptActive();
void openNamePrompt();
void renderNamePrompt();

} // namespace selva::ui
