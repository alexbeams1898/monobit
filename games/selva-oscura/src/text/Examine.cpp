#include "text/Examine.h"

#include "text/TextPresentation.h"

#include <tracy/Tracy.hpp>

namespace selva::text
{

void beginExamine(const std::string& text, const std::string& speaker)
{
    // Correlation marker for perf traces: examine opens produce a
    // "beginExamine" message so the analyzer can pair spike zones
    // with the exact input event that produced them. Cheap message
    // send; only visible when Tracy is capturing.
    TracyMessageL("beginExamine");
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
