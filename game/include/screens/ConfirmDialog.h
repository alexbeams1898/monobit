#pragma once

#include "FontManager.h"
#include "UIRenderer.h"

#include <string>
#include <vector>

class EntityManager;

// ---------------------------------------------------------------------------
// ConfirmDialog -- auto-sizing centered confirmation dialog.
//
// Measures all text content, computes panel size to fit, renders centered.
// Handles keyboard (Y/N, Left/Right, Enter, Escape) and mouse input.
// ---------------------------------------------------------------------------
namespace ConfirmDialog
{

enum class Result
{
    None,
    Yes,
    No
};

struct Options
{
    FontHandle title_font = INVALID_FONT;
    FontHandle body_font = INVALID_FONT;
    std::string title;
    std::vector<std::string> body_lines;
    int* selection = nullptr; // 0 = Yes, 1 = No (caller owns this)
    float min_width = 300.0f;
    float padding = 28.0f;
};

// Render a confirmation dialog and return the user's action.
Result render(EntityManager& em, const Options& opts, float window_w, float window_h);

} // namespace ConfirmDialog
