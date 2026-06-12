#pragma once

#include <string>

// Examine: a tiny producer for the shared text-presentation layer.
// Shows a one-line observation with no choices and a "[E] continue"
// dismiss hint. Used for environmental flavor on non-dialog beings
// (fresh larvae, future ambient observables, Grimoire-style
// narration popups).
//
// Distinct from selva::dialog: no NPC, no topic registry, no
// show_when machinery, no state persistence. Just text on the
// screen, dismissable.

namespace selva::text
{

// Open an examine session showing `text`. Replaces any active text
// session (forcibly displaces dialog if one was open -- examines
// are infrequent, so this is a rare edge case but worth documenting).
// Press E or Enter to dismiss.
//
// `speaker` is optional. Empty (default) = unattributed narration --
// the typical case, matches the Grimoire-style "Hell observing
// itself" register. Non-empty = attributed (rare for examine; if
// you want speaker attribution you probably want dialog).
void beginExamine(const std::string& text, const std::string& speaker = "");

} // namespace selva::text
