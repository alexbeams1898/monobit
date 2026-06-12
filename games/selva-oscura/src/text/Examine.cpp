#include "text/Examine.h"

#include "text/TextPresentation.h"

namespace selva::text
{

void beginExamine(const std::string& text, const std::string& speaker)
{
    ActiveTextView view;
    view.speaker = speaker;
    view.line = text;
    // Empty choices = panel shows the "[E] continue" hint.
    SessionHandlers handlers;
    handlers.on_confirm_advance = []() { end(); };
    // on_select_choice: irrelevant (no choices to select).
    // on_end: nothing to clean up -- examine has no producer state.
    begin(view, handlers);
}

} // namespace selva::text
