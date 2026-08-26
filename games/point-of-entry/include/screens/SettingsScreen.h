#pragma once

#include "screens/ScreenInput.h"
#include "screens/ScreenStyle.h"

// What can be changed about the game rather than about the job. Reachable from the title and
// from the pause sheet, and it is not the same screen from both: forgetting the work is offered
// only where there is no work underway, because erasing the file out from under a running job
// would be undone by the next thing that wrote it down.
namespace settings_screen
{

enum class Action
{
    None,
    Back,  // one step out, to wherever it was opened from
    Forget // confirmed: erase the life on disk
};

// Back to the first page. Call when the screen opens.
void reset();

// One frame of keyboard. `back` steps the ladder -- an open question backs out to the list.
Action step(bool up, bool down, bool confirm, bool back);

// `from_title` decides whether the irreversible row exists at all.
Action render(const shell_input::Mouse& mouse, bool from_title, int windowW, int windowH);

} // namespace settings_screen
