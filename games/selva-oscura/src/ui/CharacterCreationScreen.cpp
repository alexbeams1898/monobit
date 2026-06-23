#include "ui/CharacterCreationScreen.h"

#include "AppState.h"
#include "AppStateGlobal.h"
#include "Engine.h"
#include "SaveManager.h"
#include "gameplay/Actor.h"
#include "gameplay/Appearance.h"
#include "ui/CharacterPreview.h"
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
    bool confirmed_this_frame = false;
    char name_buf[kNameMaxLen + 1] = "";
    selva::gameplay::Appearance app;
};

State& state()
{
    static State s;
    return s;
}

bool isNameTaken(const char* name)
{
    if (name == nullptr || name[0] == '\0')
        return false;
    for (const auto& c : selva::saveData().characters)
        if (c.name == name)
            return true;
    return false;
}

// Render the identity-editing column. Today: Tint only. Future:
// shape sliders (face geometry, build, posture) that affect HOW the
// body looks without exposing a literal size axis -- size emerges
// from gameplay state (class, stats, evolution), not from the
// player's direct choice. The placeholder slot for those sliders
// lives here.
void drawIdentitySliders(selva::gameplay::Appearance& app)
{
    ImGui::SetWindowFontScale(1.4f);
    ImGui::TextUnformatted("Hue");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::SliderFloat("R##creator-color", &app.color.x, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("G##creator-color", &app.color.y, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("B##creator-color", &app.color.z, 0.0f, 1.0f, "%.2f");
    ImGui::ColorButton("##creator-swatch", ImVec4(app.color.x, app.color.y, app.color.z, 1.0f),
                       ImGuiColorEditFlags_NoTooltip, ImVec2(60.0f, 20.0f));
}

void drawNameField(char* buf, std::size_t buf_size)
{
    ImGui::SetWindowFontScale(1.3f);
    ImGui::TextUnformatted("Title");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::PushItemWidth(280.0f);
    ImGui::SetWindowFontScale(1.2f);
    ImGui::InputText("##creator-name", buf, buf_size);
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopItemWidth();
}

// Mirror the preview-image + orbit-input block from TuningPanel's
// Character tab. Duplicated here (vs sharing) because the screen
// owns its own layout and rejecting a click-out-of-preview to drag
// the (full-screen) window is different from the F1 panel.
void drawPreviewImage()
{
    const unsigned int tex = selva::ui::characterPreviewTexture();
    const int w = selva::ui::characterPreviewWidth();
    const int h = selva::ui::characterPreviewHeight();
    if (tex == 0 || w <= 0 || h <= 0)
    {
        ImGui::TextDisabled("(preview FBO not initialized)");
        return;
    }
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
    ImGui::ImageButton("##creator-preview-image",
                       static_cast<ImTextureID>(static_cast<std::uintptr_t>(tex)),
                       ImVec2(static_cast<float>(w), static_cast<float>(h)), ImVec2(0.0f, 1.0f),
                       ImVec2(1.0f, 0.0f));
    ImGui::PopStyleVar();
    const ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsItemHovered())
    {
        if (io.MouseWheel != 0.0f)
            selva::ui::addCharacterPreviewZoom(-io.MouseWheel * 0.10f);
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            selva::ui::resetCharacterPreviewCamera();
    }
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f))
    {
        const ImVec2 d = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.0f);
        const float dx_deg = (d.x / static_cast<float>(w)) * 360.0f;
        const float dy_deg = (d.y / static_cast<float>(h)) * 180.0f;
        selva::ui::addCharacterPreviewYaw(dx_deg);
        selva::ui::addCharacterPreviewPitch(dy_deg);
        ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
    }
    ImGui::Spacing();
    ImGui::TextDisabled("Left-drag  -  Rotate");
    ImGui::TextDisabled("Scroll  -  Zoom");
    ImGui::TextDisabled("Right-click  -  Reset");
}

// Lowercase + replace non-[a-z0-9_] with '_' so a character named
// "Alex" gets a sensible "alex" path, "BÆR" gets "b__r", etc.
// Used to derive a per-character appearance_path. Length capped
// at 6 (matches kNameMaxLen) so the path is bounded.
std::string sanitizeNameForPath(const std::string& name)
{
    std::string out;
    out.reserve(name.size());
    for (char c : name)
    {
        if (c >= 'A' && c <= 'Z')
            out.push_back(static_cast<char>(c - 'A' + 'a'));
        else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')
            out.push_back(c);
        else
            out.push_back('_');
    }
    if (out.empty())
        out = "anon";
    return out;
}

// Commit the in-flight name + appearance onto the most-recent
// (placeholder) PlayerProfile. Atomic: name + appearance_path +
// appearance file all land in the SAME call, no intermediate
// half-committed state. Returns true on success; false if the
// state is unexpected (no characters / no placeholder).
//
// PER-CHARACTER APPEARANCE FILE: every PlayerProfile gets its OWN
// JSON at config/appearances/<safe_name>.json. Two characters
// with different colors/sizes never overwrite each other. The
// shared default_humanoid.json stays untouched as the fallback /
// preset starting point.
bool commitCreation(const State& s)
{
    auto& sd = selva::saveData();
    if (sd.characters.empty())
        return false;
    // The placeholder is always at the END of the vector -- added by
    // doMainMenuAction's New Game branch immediately before opening
    // this screen.
    selva::PlayerProfile& p = sd.characters.back();
    p.name = s.name_buf;
    const std::string safe = sanitizeNameForPath(p.name);
    p.appearance_path = "config/appearances/" + safe + ".json";
    selva::gameplay::saveAppearance(p.appearance_path, s.app);
    sd.last_played_character = p.name;
    sd.has_last_played = true;
    selva::SaveManager::save(sd);
    return true;
}

} // namespace

bool characterCreationActive()
{
    return state().active && selva::gameState().phase == selva::GameState::Phase::CharacterCreation;
}

void openCharacterCreationScreen()
{
    State& s = state();
    s.active = true;
    s.confirmed_this_frame = false;
    s.name_buf[0] = '\0';
    s.app = selva::gameplay::Appearance{}; // defaults: body 1.0, head 1.0, etc.
    selva::gameState().phase = selva::GameState::Phase::CharacterCreation;
}

bool renderCharacterCreationScreen()
{
    State& s = state();
    s.confirmed_this_frame = false;
    if (!s.active)
        return false;

    // Push the in-flight Appearance onto the live player Actor every
    // frame so the existing CharacterPreview render path (which
    // reads player().appearance) reflects the slider edits. The
    // actor pool was initialized at boot; the player Actor exists
    // even before any profile is active.
    selva::gameplay::player().appearance = s.app;

    // Render the preview FBO from inside the ImGui render path: the
    // gameplay per-frame tick only renders the preview during
    // Phase::Playing, and we're in CharacterCreation. Without this,
    // the FBO ImGui::Image samples is stale (whatever the last
    // Playing frame left). Done BEFORE the layout so the texture
    // is fresh when sampled below.
    selva::ui::renderCharacterPreview();

    // Full-screen Souls-style modal with a backdrop.
    drawFullScreenBackdrop(0.65f);
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const ImVec2 panel_size(vp->Size.x * 0.85f, vp->Size.y * 0.85f);
    beginCenteredWindow("##creator", panel_size);
    ImGui::Spacing();

    const float preview_col_w = static_cast<float>(selva::ui::characterPreviewWidth()) + 16.0f;
    const float avail_w = ImGui::GetContentRegionAvail().x;
    const float slider_col_w = std::max(280.0f, avail_w - preview_col_w);

    ImGui::BeginChild("##creator-sliders", ImVec2(slider_col_w, 0), false);
    // Name is the primary identity act; takes top-of-column +
    // larger header font. Form follows below.
    drawNameField(s.name_buf, sizeof(s.name_buf));
    ImGui::Spacing();
    ImGui::Spacing();
    drawIdentitySliders(s.app);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Confirm gate: name must be non-empty, not all whitespace, and
    // not already taken by another save-slot character. The taken
    // check skips the trailing placeholder slot (which holds this
    // creation in progress).
    const std::string trimmed = [&]
    {
        std::string out = s.name_buf;
        while (!out.empty() && (out.back() == ' ' || out.back() == '\t'))
            out.pop_back();
        return out;
    }();
    bool name_ok = !trimmed.empty();
    if (name_ok)
    {
        // Re-check uniqueness against everything EXCEPT the
        // placeholder at the end of the vector.
        auto& chars = selva::saveData().characters;
        for (std::size_t i = 0; i + 1 < chars.size(); ++i)
            if (chars[i].name == trimmed)
                name_ok = false;
    }

    // Centered confirm. Width derived from column so the button
    // visually anchors the bottom of the slider column. No
    // trailing period -- buttons aren't sentences.
    if (!name_ok)
        ImGui::BeginDisabled();
    const float button_w = 240.0f;
    const float column_w = ImGui::GetContentRegionAvail().x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (column_w - button_w) * 0.5f);
    if (ImGui::Button("Take this form", ImVec2(button_w, 36.0f)))
    {
        if (commitCreation(s))
        {
            s.active = false;
            s.confirmed_this_frame = true;
        }
    }
    if (!name_ok)
        ImGui::EndDisabled();

    if (!name_ok && !trimmed.empty())
    {
        ImGui::Spacing();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (column_w - 200.0f) * 0.5f);
        ImGui::TextDisabled("(that name is already borne)");
    }

    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("##creator-preview", ImVec2(0, 0), false);
    drawPreviewImage();
    ImGui::EndChild();

    ImGui::End();

    return s.confirmed_this_frame;
}

} // namespace selva::ui
