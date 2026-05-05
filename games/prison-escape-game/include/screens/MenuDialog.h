#pragma once

#include "FontManager.h"
#include "UIRenderer.h"

#include <string>
#include <vector>

class EntityManager;

// ---------------------------------------------------------------------------
// MenuDialog -- auto-sizing centered option-list dialog.
//
// Measures title, all option labels + descriptions, and hint text to compute
// panel dimensions. Handles keyboard (Up/Down, Enter, Escape) and mouse
// (hover to select, click to activate, RMB to dismiss).
// ---------------------------------------------------------------------------
namespace MenuDialog
{

struct Option
{
    std::string label;
    std::string description;
    bool enabled = true;
};

// Returned when the user activates an option or dismisses the dialog.
struct Result
{
    int selected = -1; // index of activated option, or -1 if dismissed/none
    bool dismissed = false;
};

struct Options
{
    FontHandle title_font = INVALID_FONT;
    FontHandle body_font = INVALID_FONT;
    std::string title;
    std::vector<Option> items;
    std::string hint;         // bottom hint text (e.g. "[Enter] Select  [Esc] Leave")
    int* selection = nullptr; // caller-owned cursor index
    float min_width = 360.0f;
    float padding = 24.0f;
    bool close_on_escape = true;
    bool close_on_rmb = true;
    bool darken_background = true;
};

Result render(EntityManager& em, const Options& opts, float window_w, float window_h);

} // namespace MenuDialog
