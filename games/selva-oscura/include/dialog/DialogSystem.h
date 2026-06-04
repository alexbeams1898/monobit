#pragma once

#include <string>
#include <vector>

namespace selva::dialog
{

// Read-only snapshot of the active topic, for UI rendering. Pointer
// returned by currentView() invalidates on the next tick / state
// transition; copy if you need to hold it across frames.
struct ActiveTopicView
{
    std::string npc_id;
    std::string topic_id;
    std::string speaker_display_name; // empty for unattributed (vagrant / narration)
    std::string line;

    struct ChoiceView
    {
        std::string id;
        std::string label;
        bool enabled = true;
        std::string disabled_reason; // shown as tooltip when !enabled
    };
    std::vector<ChoiceView> choices;
};

// Begin a conversation with the named NPC. Resolves npc_id against
// the TopicRegistry. Picks the first topic whose show_when passes
// as the entry. No-op + warning log if: NPC not in registry, no
// topic passes show_when, OR a dialog is already active.
void begin(const std::string& npc_id);

// Force-end the current dialog. Safe to call when no dialog is
// active. Fires on_exit handlers for the current topic.
void end();

// True while a dialog is being displayed.
bool active();

// Per-frame tick. Currently a no-op (transitions are driven by
// selectChoice / confirmAdvance edges). Reserved for future
// auto-advance / typewriter effects.
void tick();

// UI accessor. Returns nullptr if no dialog is active.
const ActiveTopicView* currentView();

// Pick a choice by its index in currentView()->choices. No-op if
// not in dialog, index out of range, or the choice is disabled.
// Fires the choice's on_select handler, then the current topic's
// on_exit handler, then transitions to next_topic. Special
// next_topic values: "__end__" ends the dialog; "__same__" stays
// on the same topic (re-evaluates choice gates).
void selectChoice(int choice_index);

// Advance an empty-choices topic. Fires on_exit and ends the
// dialog. No-op if not in dialog or the current topic has choices.
void confirmAdvance();

// Clear all runtime state. Called from hardResetWorldForCharacter
// so a dialog mid-conversation doesn't survive a character switch.
void hardReset();

} // namespace selva::dialog
