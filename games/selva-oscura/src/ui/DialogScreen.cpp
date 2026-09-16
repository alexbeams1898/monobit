#include "ui/DialogScreen.h"

#include "text/TextPresentation.h"

#include <imgui.h>

#include <cstdio>
#include <string>

// Renders the shared text-presentation panel. Reads
// selva::text::currentView() and routes input to
// selva::text::selectChoice / confirmAdvance. The producer
// (DialogSystem, Examine, future narration) is invisible from here
// -- one panel, many drivers.
namespace selva::ui
{

namespace
{

constexpr float kBoxWidthFraction = 0.55f; // panel ~55% of viewport width
constexpr float kBoxBottomMargin = 0.10f;  // panel bottom edge 10% above viewport bottom
constexpr float kBoxMinHeight = 130.0f;    // floor on the line panel
constexpr float kChoiceRowHeight = 28.0f;  // per-choice button height
constexpr float kChoiceSpacing = 6.0f;
constexpr float kSpeakerNameFontScale = 1.05f;
constexpr float kLineFontScale = 1.10f;

// Persistent highlight cursor for keyboard nav. Tracked across
// frames; resets when the displayed line changes (different content =
// new session moment, default to first enabled choice).
//
// keyboard_owns: last-input-source-wins. Keyboard nav (arrows) sets
// it true; real mouse movement sets it false. Choice loop only lets
// mouse hover steer the cursor when keyboard_owns is false -- so
// keyboard-selected option doesn't flicker when the mouse happens
// to be stationary over a different row.
struct CursorState
{
    std::string for_view_key; // hash-equivalent: speaker + "/" + line, swaps reset cursor
    int index = 0;
    bool keyboard_owns = false;
};
CursorState& cursor()
{
    static CursorState c;
    return c;
}

// True if the user moved the mouse this frame (non-zero delta).
// Used to flip cursor ownership back to mouse.
bool mouseMovedThisFrame()
{
    const ImVec2 d = ImGui::GetIO().MouseDelta;
    return (d.x != 0.0f) || (d.y != 0.0f);
}

// Step the cursor index by delta, skipping disabled choices. Wraps
// at top/bottom. No-op if all choices are disabled. Always marks
// keyboard as the cursor owner.
void stepCursor(const selva::text::ActiveTextView& view, int delta)
{
    const int n = static_cast<int>(view.choices.size());
    if (n <= 0)
        return;
    int i = cursor().index;
    for (int tries = 0; tries < n; ++tries)
    {
        i = ((i + delta) % n + n) % n;
        if (view.choices[static_cast<std::size_t>(i)].enabled)
        {
            cursor().index = i;
            cursor().keyboard_owns = true;
            return;
        }
    }
}

// Find the first enabled choice index; -1 if none.
int firstEnabledIndex(const selva::text::ActiveTextView& view)
{
    const int n = static_cast<int>(view.choices.size());
    for (int i = 0; i < n; ++i)
        if (view.choices[static_cast<std::size_t>(i)].enabled)
            return i;
    return -1;
}

// Sync the cursor to the current view. Resets to first enabled
// choice when content changes; clamps if current index is now
// disabled or out of range.
void syncCursor(const selva::text::ActiveTextView& view)
{
    const std::string key = view.speaker + "/" + view.line;
    auto& c = cursor();
    if (c.for_view_key != key)
    {
        c.for_view_key = key;
        c.index = firstEnabledIndex(view);
        if (c.index < 0)
            c.index = 0;
        // New content: assume keyboard owns the cursor until the player
        // actively moves the mouse. Otherwise the default selection
        // would be silently overridden by stale mouse position the
        // moment the panel appears.
        c.keyboard_owns = true;
        return;
    }
    // Same content, but the choice at cursor might have become disabled
    // (custom condition re-evaluated). Clamp to a still-enabled one.
    const int n = static_cast<int>(view.choices.size());
    if (c.index < 0 || c.index >= n || !view.choices[static_cast<std::size_t>(c.index)].enabled)
    {
        c.index = firstEnabledIndex(view);
        if (c.index < 0)
            c.index = 0;
    }
}

void handleHotkeys(const selva::text::ActiveTextView& view)
{
    // Empty-choices view: Enter or E advances.
    if (view.choices.empty())
    {
        if (ImGui::IsKeyPressed(ImGuiKey_Enter, /*repeat=*/false) ||
            ImGui::IsKeyPressed(ImGuiKey_E, /*repeat=*/false))
        {
            selva::text::confirmAdvance();
        }
        return;
    }
    // Arrow keys / WS step the cursor among enabled choices.
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, /*repeat=*/true) ||
        ImGui::IsKeyPressed(ImGuiKey_S, /*repeat=*/true))
    {
        stepCursor(view, +1);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, /*repeat=*/true) ||
        ImGui::IsKeyPressed(ImGuiKey_W, /*repeat=*/true))
    {
        stepCursor(view, -1);
    }
    // E or Enter commits the highlighted choice.
    if (ImGui::IsKeyPressed(ImGuiKey_E, /*repeat=*/false) ||
        ImGui::IsKeyPressed(ImGuiKey_Enter, /*repeat=*/false))
    {
        const int idx = cursor().index;
        if (idx >= 0 && idx < static_cast<int>(view.choices.size()) &&
            view.choices[static_cast<std::size_t>(idx)].enabled)
        {
            selva::text::selectChoice(idx);
        }
    }
}

} // namespace

bool dialogConsumesInput()
{
    return selva::text::active();
}

void renderDialogScreen()
{
    const selva::text::ActiveTextView* view = selva::text::currentView();
    if (view == nullptr)
        return;

    // Last-input-source-wins: if the mouse moved this frame, flip
    // ownership back to mouse so hover can steer the cursor. If
    // mouse was stationary, keyboard retains ownership and hover
    // can't overwrite the keyboard-selected row (the flicker fix).
    if (mouseMovedThisFrame())
        cursor().keyboard_owns = false;

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float vw = vp->Size.x;
    const float vh = vp->Size.y;
    const float box_w = vw * kBoxWidthFraction;
    const float box_x = vp->Pos.x + (vw - box_w) * 0.5f;
    // Box height grows with line text; choice list sits ABOVE the box
    // so the line stays in a consistent screen position.
    const float box_h = kBoxMinHeight;
    const float box_y = vp->Pos.y + vh * (1.0f - kBoxBottomMargin) - box_h;

    // Background panel (line + speaker).
    ImGui::SetNextWindowPos(ImVec2(box_x, box_y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(box_w, box_h), ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.05f, 0.04f, 0.03f, 0.92f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.55f, 0.45f, 0.30f, 0.85f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.5f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f, 14.0f));
    ImGui::Begin("##dialog_box", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                     ImGuiWindowFlags_NoNav);

    if (!view->speaker.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.78f, 0.62f, 1.0f));
        ImGui::SetWindowFontScale(kSpeakerNameFontScale);
        ImGui::TextUnformatted(view->speaker.c_str());
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();
        ImGui::Separator();
        ImGui::Spacing();
    }
    ImGui::SetWindowFontScale(kLineFontScale);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.92f, 0.86f, 1.0f));
    ImGui::TextWrapped("%s", view->line.c_str());
    ImGui::PopStyleColor();
    ImGui::SetWindowFontScale(1.0f);

    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);

    // Choices panel sits BELOW the line box. Empty-choices view
    // shows a small "[E] continue" hint in the same slot.
    if (view->choices.empty())
    {
        const float hint_h = 28.0f;
        const float hint_w = 200.0f;
        const float hint_x = vp->Pos.x + (vw - hint_w) * 0.5f;
        const float hint_y = box_y + box_h + 6.0f;
        ImGui::SetNextWindowPos(ImVec2(hint_x, hint_y), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(hint_w, hint_h), ImGuiCond_Always);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::Begin("##dialog_hint", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                         ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
                         ImGuiWindowFlags_NoBackground);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 0.70f, 0.55f, 0.9f));
        ImGui::TextUnformatted("[E] continue");
        ImGui::PopStyleColor();
        ImGui::End();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }
    else
    {
        syncCursor(*view);
        const int n = static_cast<int>(view->choices.size());
        const float choices_h = static_cast<float>(n) * (kChoiceRowHeight + kChoiceSpacing) + 20.0f;
        ImGui::SetNextWindowPos(ImVec2(box_x, box_y + box_h + 6.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(box_w, choices_h), ImGuiCond_Always);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.04f, 0.03f, 0.02f, 0.85f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.55f, 0.45f, 0.30f, 0.7f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 10.0f));
        ImGui::Begin("##dialog_choices", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                         ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoFocusOnAppearing);
        // Single visual cue for selection: the "> " marker shifts to
        // whichever row the cursor (keyboard or mouse) points at. No
        // row-fill, no hover background -- the marker IS the cue.
        // Selectable's own fill colors are pushed transparent so click
        // activation works without painting a second decoration on
        // top of the marker.
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        const int cursor_idx = cursor().index;
        for (int i = 0; i < n; ++i)
        {
            const auto& cv = view->choices[static_cast<std::size_t>(i)];
            const std::string marker = (i == cursor_idx) ? "> " : "  ";
            const std::string label = marker + cv.label;
            if (!cv.enabled)
                ImGui::BeginDisabled();
            if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_None,
                                  ImVec2(0.0f, kChoiceRowHeight)))
            {
                // selectChoice routes to the active session's handler
                // (deferred-intent in dialog producer's case). Safe to
                // call mid-iteration; view stays valid.
                selva::text::selectChoice(i);
            }
            // Mouse hover steers the cursor ONLY if keyboard isn't
            // the active owner. Otherwise the keyboard-selected
            // option would visibly snap to wherever the mouse rests.
            // Last-input-source wins.
            if (cv.enabled && !cursor().keyboard_owns && ImGui::IsItemHovered())
                cursor().index = i;
            if (!cv.enabled)
            {
                ImGui::EndDisabled();
                if (!cv.disabled_reason.empty() && ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", cv.disabled_reason.c_str());
            }
        }
        ImGui::PopStyleColor(3);
        ImGui::End();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(2);
    }

    handleHotkeys(*view);
}

} // namespace selva::ui
