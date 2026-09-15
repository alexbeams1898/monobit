#pragma once

namespace selva::ui
{

// Render the dialog box if a dialog is active. A
// center-bottom panel with speaker name, NPC line, and the choice
// list. No-op when selva::dialog::active() is false.
//
// Also handles input edges while dialog is active: number keys 1-9
// pick choices, Enter / E confirms an empty-choices topic. Input
// to attack (LMB/RMB) is soft-suppressed at the dialog level so
// the player can't swing through a conversation.
void renderDialogScreen();

// True while the dialog screen is consuming player input (combat
// inputs should be gated by this so the player can't attack while
// reading a line). Mirrors selva::dialog::active() but lives in
// the UI layer so combat code doesn't include dialog headers.
bool dialogConsumesInput();

} // namespace selva::ui
