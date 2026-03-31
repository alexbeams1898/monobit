#pragma once

#include "UIRenderer.h"

// ---------------------------------------------------------------------------
// Shared UI color palette.  Screens import via `using namespace screen_colors;`
// and delete their local copies.  Per-screen overrides (e.g. ConfirmDialog's
// red TITLE_COLOR) stay in each .cpp as local constexpr.
// ---------------------------------------------------------------------------

namespace screen_colors
{

// Text
constexpr Color TEXT_WHITE{0.92f, 0.90f, 0.88f, 1.0f};
constexpr Color TEXT_DIM{0.5f, 0.48f, 0.46f, 1.0f};

// Overlays (two tiers: medium for in-game popups, opaque for full-screen menus)
constexpr Color OVERLAY{0.0f, 0.0f, 0.0f, 0.75f};
constexpr Color OVERLAY_OPAQUE{0.0f, 0.0f, 0.0f, 0.92f};

// Panel background
constexpr Color PANEL_BG{0.06f, 0.06f, 0.09f, 0.95f};

// Standard button colors
constexpr Color BTN_NORMAL{0.7f, 0.68f, 0.65f, 1.0f};
constexpr Color BTN_HOVER{0.95f, 0.88f, 0.55f, 1.0f};
constexpr Color BTN_BG{0.1f, 0.1f, 0.12f, 0.5f};
constexpr Color BTN_BG_HL{0.18f, 0.16f, 0.25f, 0.7f};

} // namespace screen_colors
