#pragma once

// Shared slider surface for editing a selva::gameplay::Appearance.
// Used by BOTH the runtime character creator (CharacterCreationScreen)
// and the Effigie (standalone designer tool).
//
// Doctrinal shape: the Appearance struct is the canonical authored
// data. This module owns the presentation layer -- category list,
// per-category slider blocks, hair/eye/hue special surfaces. Each
// callsite provides its own State (undo stack, camera framing, name
// input, save/load chrome); this module only edits the Appearance in
// place.

#include "gameplay/Appearance.h"

#include <cstddef>
#include <string>

namespace selva::ui
{

// Top-level rail entry: JSON category id + player-facing label.
struct AppearanceCategory
{
    const char* id;
    const char* label;
};

// The ordered list of categories that show up in the rail. Same list
// on both surfaces so runtime creator + Effigie stay in visual sync.
const AppearanceCategory* appearanceCategories();
std::size_t appearanceCategoryCount();

// Per-category preview-camera framing (yaw / pitch / zoom / look-at
// fraction). Used by callers to snap the preview when the user picks
// a rail entry. NaN in any field = "leave that knob unchanged."
struct AppearanceCategoryFraming
{
    const char* id;
    float yaw_degrees;
    float pitch_degrees;
    float zoom;
    float look_at_fraction;
};
const AppearanceCategoryFraming* findAppearanceCategoryFraming(const std::string& cat_id);

// True if the category has any content to show. Special-cased
// categories (identity, eyes, hair, hue) return true unconditionally;
// registry-driven categories return true only if there's at least one
// player-visible slider registered under that id.
//
// When include_designer_only=true (Effigie), ignore the
// player_visible filter -- every registered slider counts.
bool appearanceCategoryHasContent(const char* cat_id, bool include_designer_only);

// Draw the right-pane content for the given category id. Dispatches to
// the identity/eyes/hair/hue special blocks or falls through to a
// generic slider loop.
//
// `edit_committed_out` is set to true when the user finishes an edit
// (drag released / discrete pick made), letting the caller push an
// undo history entry.
//
// `include_designer_only` = true surfaces every registered slider,
// including ones marked `player_visible: false` in
// config/appearances/sliders.json. Runtime creator passes false; the
// Effigie's designer-only toggle drives this.
void drawAppearanceCategoryContent(selva::gameplay::Appearance& appearance,
                                   const std::string& cat_id, bool include_designer_only,
                                   bool* edit_committed_out);

} // namespace selva::ui
