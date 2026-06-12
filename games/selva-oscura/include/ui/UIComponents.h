#pragma once

// Shared UI components reused across menus, modals, and HUD layers.
//
// Doctrine: every screen and modal uses these primitives. New surfaces
// should be a composition of these helpers + screen-specific content,
// not a from-scratch ImGui block. When something obviously belongs in
// the shared layer (centered windows, hint bars, back gestures, the
// roman-numeral renderer, etc.) it lives HERE, not in the file that
// happens to need it first.
//
// All keyboard / mouse input flows through ImGui::IsKeyPressed +
// ImGui::IsMouseClicked. NEVER use SDL_PollEvent inside menu/modal
// code -- the engine's central event pump owns SDL events and feeds
// them into ImGui; pulling them off the queue in screen-side code
// steals events that other systems need.

#include "gameplay/RomanNumeral.h"

#include <imgui.h>

#include <cstdint>

namespace selva::ui
{

// ---------------------------------------------------------------------------
// Windowing primitives
// ---------------------------------------------------------------------------

// Center the next window on the viewport at the given size, undecorated.
// Caller must follow with ImGui::End().
void beginCenteredWindow(const char* name, ImVec2 size);

// Full-screen dimming layer. Drawn as a backdrop window behind a
// modal so the world reads as suspended. Alpha 0.0..1.0; 0.85 is the
// canonical "world but not seen" weight.
void drawFullScreenBackdrop(float alpha = 0.85f);

// Below-the-modal hint bar (e.g. "[Esc/RMB] Resume"). Borderless,
// transparent, anchored just below a modal of the given size.
void drawHintBar(const char* text, ImVec2 modal_size);

// ---------------------------------------------------------------------------
// Input gestures
// ---------------------------------------------------------------------------

// True on the frame RMB is pressed. ImGui-driven; safe in menus where
// the cursor is visible. In Playing the cursor is captured for mouse-
// look so this won't fire.
bool rmbClicked();

// Back/cancel: ESC or RMB. Consume in exactly one place per frame.
bool wantBack();

// ---------------------------------------------------------------------------
// Button helpers
// ---------------------------------------------------------------------------

// Fixed-width button horizontally centered in the current content
// region.
bool centeredButton(const char* label, float width = 200.0f);

// ---------------------------------------------------------------------------
// Roman numeral rendering (vinculum-aware)
// ---------------------------------------------------------------------------

// Draw a RomanRendering at the given screen pos using the active
// ImGui font. Renders single + double vinculum bars over the marked
// glyphs (drawn manually because ImGui's default font lacks
// combining diacritics). Returns the advance width consumed so the
// caller can lay out adjacent text.
//
// Lives in the shared component module so the HUD vessel counter,
// the class picker's stat readout, and any future roman-numeral
// surface share one renderer.
float drawRomanGlyphs(ImDrawList* draw, const ImVec2& pos,
                      const selva::gameplay::RomanRendering& rendering, ImU32 color);

} // namespace selva::ui
