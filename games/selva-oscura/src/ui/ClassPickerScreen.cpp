#include "ui/ClassPickerScreen.h"

#include "AppStateGlobal.h"

#include <imgui.h>

#include <SDL.h>

#include <array>
#include <cstdio>

namespace selva::ui
{

namespace
{

// Modal state. Single global instance -- only one Signing happens per
// character at any time. The two-step confirm pattern:
//   1. Player highlights an option (up/down arrows or hover)
//   2. Player presses Enter / A → state advances to Confirming
//   3. Player presses Enter / A again → commit fires
//   4. Player presses Escape from Confirming → return to picking (cancel
//      the confirmation, NOT the modal)
//
// No back button at any stage: the Signing is irrevocable per locked canon.
// The "Escape" cancel only walks back the second-step confirmation, never
// dismisses the modal entirely. The player can change which class they're
// about to pick but cannot back out of picking.
enum class Phase : std::uint8_t
{
    Inactive = 0,
    Picking,
    Confirming,
};

struct State
{
    Phase phase = Phase::Inactive;
    PlayerClass cursor = PlayerClass::Penitent;
    PlayerClass confirming = PlayerClass::None;
};

State& state()
{
    static State s;
    return s;
}

struct Option
{
    PlayerClass cls;
    const char* label;       // "Penitent" / "Heretic" / "Wretched"
    const char* description; // one-line cosmological framing
};

// Three options. Refusal is handled at the dialog layer (the Guide's
// signing_refused branch) and commits directly to Unburdened; this
// modal only fires when the player accepted the Signing, and the
// choice is which class identity to receive. Per the locked sequence:
// dialog accept/refuse -> if accept, modal Penitent/Heretic/Wretched.
constexpr std::array<Option, 3> kOptions = {{
    {PlayerClass::Penitent, "Penitent",
     "Bend the head. Receive Hell's measurement. Walk the path bent."},
    {PlayerClass::Heretic, "Heretic",
     "Bend the head. Receive Hell's measurement. Carry it crooked."},
    {PlayerClass::Wretched, "Wretched",
     "Bend the head. Receive Hell's measurement. Refuse completion."},
}};

int optionIndexOf(PlayerClass c)
{
    for (std::size_t i = 0; i < kOptions.size(); ++i)
        if (kOptions[i].cls == c)
            return static_cast<int>(i);
    return 0;
}

void commitClass(PlayerClass c)
{
    auto* profile = activePlayerProfile();
    if (profile != nullptr)
        profile->player_class = c;
    // Modal commits the three signed-class identities. Refusal is
    // handled at the dialog layer (guide_commit_refusal) and never
    // routes through here. The path flag mirrors the cosmological
    // identity so the existing dialog branches still work.
    if (c != PlayerClass::None)
    {
        setFlag("path_class_picker");
        clearFlag("path_unburdened");
    }
    std::fprintf(stderr, "[class-picker] committed class=%s\n", playerClassName(c));
    std::fflush(stderr);
}

void handlePickingInput(SDL_Keycode key)
{
    auto& s = state();
    if (key == SDLK_UP || key == SDLK_w)
    {
        int idx = optionIndexOf(s.cursor);
        idx = (idx + static_cast<int>(kOptions.size()) - 1) % static_cast<int>(kOptions.size());
        s.cursor = kOptions[idx].cls;
    }
    else if (key == SDLK_DOWN || key == SDLK_s)
    {
        int idx = optionIndexOf(s.cursor);
        idx = (idx + 1) % static_cast<int>(kOptions.size());
        s.cursor = kOptions[idx].cls;
    }
    else if (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_e)
    {
        s.phase = Phase::Confirming;
        s.confirming = s.cursor;
    }
}

void handleConfirmingInput(SDL_Keycode key)
{
    auto& s = state();
    if (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_e)
    {
        commitClass(s.confirming);
        s.phase = Phase::Inactive;
        s.confirming = PlayerClass::None;
    }
    else if (key == SDLK_ESCAPE)
    {
        // Cancel the confirmation step ONLY. Return to picking. The
        // modal itself can't be dismissed.
        s.phase = Phase::Picking;
        s.confirming = PlayerClass::None;
    }
}

void pollInput()
{
    auto& s = state();
    if (s.phase == Phase::Inactive)
        return;
    // Edge-triggered keyboard input via SDL events sitting in the queue.
    // ImGui's IO captures keyboard but we don't want text input behavior
    // here -- direct SDL polling matches how other modals (Pause, etc.)
    // are driven and avoids the lag of waiting for ImGui's next-frame
    // edge.
    SDL_Event ev;
    while (SDL_PollEvent(&ev))
    {
        if (ev.type != SDL_KEYDOWN)
            continue;
        const SDL_Keycode key = ev.key.keysym.sym;
        if (s.phase == Phase::Picking)
            handlePickingInput(key);
        else if (s.phase == Phase::Confirming)
            handleConfirmingInput(key);
    }
}

void drawBackdrop(const ImGuiViewport* vp)
{
    // Full-screen dimming layer behind the modal so the world reads as
    // suspended. ImGui background window over the whole viewport.
    ImGui::SetNextWindowPos(vp->Pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(vp->Size, ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.85f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::Begin("##class_picker_backdrop", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                     ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

void drawPicking(const ImGuiViewport* vp)
{
    const float panel_w = vp->Size.x * 0.65f;
    const float panel_h = vp->Size.y * 0.70f;
    const ImVec2 panel_pos(vp->Pos.x + (vp->Size.x - panel_w) * 0.5f,
                           vp->Pos.y + (vp->Size.y - panel_h) * 0.5f);
    ImGui::SetNextWindowPos(panel_pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(panel_w, panel_h), ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.06f, 0.04f, 0.03f, 0.96f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.60f, 0.48f, 0.32f, 0.95f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(36.0f, 28.0f));
    ImGui::Begin("##class_picker_panel", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                     ImGuiWindowFlags_NoNav);

    // Title -- the Signing, framed as the irrevocable moment.
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.92f, 0.85f, 0.68f, 1.0f));
    ImGui::SetWindowFontScale(1.4f);
    ImGui::TextUnformatted("The Signing");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleColor();
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 0.68f, 0.55f, 0.9f));
    ImGui::TextWrapped("What is set here cannot be unset. Choose what thou wilt be.");
    ImGui::PopStyleColor();
    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Options.
    const auto& cursor_cls = state().cursor;
    for (const auto& opt : kOptions)
    {
        const bool selected = (opt.cls == cursor_cls);
        const ImVec4 row_color =
            selected ? ImVec4(0.95f, 0.86f, 0.55f, 1.0f) : ImVec4(0.65f, 0.58f, 0.48f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, row_color);
        ImGui::SetWindowFontScale(selected ? 1.20f : 1.10f);
        ImGui::Text("%s  %s", selected ? ">" : " ", opt.label);
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.50f, 0.42f, 0.95f));
        ImGui::Indent(28.0f);
        ImGui::TextWrapped("%s", opt.description);
        ImGui::Unindent(28.0f);
        ImGui::PopStyleColor();
        ImGui::Spacing();
    }

    // Controls hint.
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.50f, 0.42f, 0.85f));
    ImGui::TextUnformatted("[Up/Down] choose       [Enter] commit");
    ImGui::PopStyleColor();

    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

void drawConfirming(const ImGuiViewport* vp)
{
    const float panel_w = vp->Size.x * 0.50f;
    const float panel_h = vp->Size.y * 0.40f;
    const ImVec2 panel_pos(vp->Pos.x + (vp->Size.x - panel_w) * 0.5f,
                           vp->Pos.y + (vp->Size.y - panel_h) * 0.5f);
    ImGui::SetNextWindowPos(panel_pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(panel_w, panel_h), ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.05f, 0.03f, 0.02f, 0.98f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.78f, 0.55f, 0.30f, 0.98f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.5f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(36.0f, 28.0f));
    ImGui::Begin("##class_picker_confirm", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                     ImGuiWindowFlags_NoNav);

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.86f, 0.55f, 1.0f));
    ImGui::SetWindowFontScale(1.3f);
    ImGui::TextUnformatted("Confirm");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleColor();
    ImGui::Spacing();
    ImGui::Spacing();

    const PlayerClass to_commit = state().confirming;
    const char* label = "?";
    for (const auto& opt : kOptions)
        if (opt.cls == to_commit)
            label = opt.label;

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.92f, 0.85f, 0.68f, 1.0f));
    ImGui::SetWindowFontScale(1.15f);
    ImGui::Text("Thou wilt be: %s.", label);
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleColor();
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.78f, 0.55f, 0.30f, 1.0f));
    ImGui::TextWrapped(
        "This cannot be undone here. Press [Enter] to commit, or [Esc] to choose again.");
    ImGui::PopStyleColor();

    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

} // namespace

bool classPickerActive()
{
    return state().phase != Phase::Inactive;
}

void openClassPicker(PlayerClass initial_selection)
{
    auto& s = state();
    s.phase = Phase::Picking;
    s.cursor = (initial_selection == PlayerClass::None) ? PlayerClass::Penitent : initial_selection;
    s.confirming = PlayerClass::None;
}

void renderClassPicker()
{
    auto& s = state();
    if (s.phase == Phase::Inactive)
        return;
    pollInput();
    if (s.phase == Phase::Inactive)
        return; // commit fired during input handling -- nothing to draw
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    drawBackdrop(vp);
    if (s.phase == Phase::Picking)
        drawPicking(vp);
    else if (s.phase == Phase::Confirming)
        drawConfirming(vp);
}

} // namespace selva::ui
