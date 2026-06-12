#include "ui/NamePromptScreen.h"

#include "AppStateGlobal.h"
#include "ui/ClassPickerScreen.h"
#include "ui/UIComponents.h"

#include <imgui.h>

#include <cstdint>
#include <cstring>
#include <string>

namespace selva::ui
{

namespace
{

constexpr int kNameMaxLen = 6;

struct State
{
    bool active = false;
    char buf[kNameMaxLen + 1] = "";
};

State& state()
{
    static State s;
    return s;
}

bool isNameTaken(const char* name)
{
    for (const auto& c : saveData().characters)
        if (c.name == name)
            return true;
    return false;
}

bool isNameValid(const char* name, bool& out_taken)
{
    out_taken = false;
    if (name == nullptr || name[0] == '\0')
        return false;
    if (isNameTaken(name))
    {
        out_taken = true;
        return false;
    }
    return true;
}

// Commit the typed name: rename the active character's profile
// in-place + sync gameState::active_character + raise the
// name_given flag + chain into the class picker.
//
// Unnamed-but-real character pattern: the active profile already
// exists in saveData (name=="" until this commit); we mutate name
// directly. active_character must be updated in lockstep because
// it's the lookup key for activePlayerProfile() -- without that,
// the next frame's lookups would fail (no profile matches the
// new name yet vs. active_character pointing at "").
//
// Naming + class-pick are ONE character-creation sequence; chaining
// the two modals avoids forcing the player back into world-play
// between them just to re-engage the Guide. The next pause-open
// autosave persists the renamed profile to disk.
void commit(const char* name)
{
    PlayerProfile* profile = selva::activePlayerProfile();
    if (profile != nullptr)
    {
        profile->name = name;
        selva::gameState().active_character = name;
    }
    selva::setFlag("name_given");
    std::fprintf(stderr, "[name-prompt] committed name='%s' -> opening class picker\n", name);
    std::fflush(stderr);
    selva::ui::openClassPicker();
}

void handleKeyboard()
{
    auto& s = state();
    bool taken = false;
    const bool valid = isNameValid(s.buf, taken);
    if (!valid)
        return;
    const bool commit_pressed = ImGui::IsKeyPressed(ImGuiKey_Enter, /*repeat=*/false) ||
                                ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, /*repeat=*/false);
    if (commit_pressed)
    {
        commit(s.buf);
        s.active = false;
        std::memset(s.buf, 0, sizeof(s.buf));
    }
}

} // namespace

bool namePromptActive()
{
    return state().active;
}

void openNamePrompt()
{
    auto& s = state();
    s.active = true;
    std::memset(s.buf, 0, sizeof(s.buf));
}

void renderNamePrompt()
{
    auto& s = state();
    if (!s.active)
        return;
    // Same backdrop weight as the class picker so the two Beat-4
    // modals read as the same "ritual is happening" moment.
    selva::ui::drawFullScreenBackdrop(0.85f);

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float panel_w = vp->Size.x * 0.45f;
    const float panel_h = vp->Size.y * 0.30f;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.06f, 0.04f, 0.03f, 0.98f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.60f, 0.48f, 0.32f, 0.95f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(36.0f, 28.0f));
    selva::ui::beginCenteredWindow("##name_prompt", ImVec2(panel_w, panel_h));

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.92f, 0.85f, 0.68f, 1.0f));
    ImGui::SetWindowFontScale(1.3f);
    ImGui::TextUnformatted("What shall I call thee?");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleColor();
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.65f, 0.58f, 0.48f, 0.90f));
    ImGui::TextUnformatted("(six letters)");
    ImGui::PopStyleColor();
    ImGui::Spacing();
    ImGui::Spacing();

    // Auto-focus the input on first frame so the player can type
    // immediately without needing to click into the field.
    if (ImGui::IsWindowAppearing())
        ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputText("##name", s.buf, sizeof(s.buf),
                     ImGuiInputTextFlags_CharsUppercase | ImGuiInputTextFlags_CharsNoBlank);

    ImGui::Spacing();
    bool taken = false;
    const bool valid = isNameValid(s.buf, taken);
    if (taken)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.45f, 0.40f, 1.0f));
        ImGui::TextUnformatted("That name is already taken.");
        ImGui::PopStyleColor();
    }
    else
    {
        ImGui::NewLine();
    }
    ImGui::Spacing();
    ImGui::Spacing();

    if (!valid)
        ImGui::BeginDisabled();
    if (selva::ui::centeredButton("Commit", 180.0f))
    {
        commit(s.buf);
        s.active = false;
        std::memset(s.buf, 0, sizeof(s.buf));
    }
    if (!valid)
        ImGui::EndDisabled();

    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);

    // Handle keyboard AFTER draw so Enter-on-the-input doesn't get
    // double-processed (ImGui's InputText already swallows Enter
    // when the field is focused; we still check IsKeyPressed for
    // edge cases like the button being focused).
    handleKeyboard();
}

} // namespace selva::ui
