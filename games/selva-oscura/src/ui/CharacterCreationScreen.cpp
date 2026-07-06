#include "ui/CharacterCreationScreen.h"

#include "AppState.h"
#include "AppStateGlobal.h"
#include "Engine.h"
#include "SaveManager.h"
#include "anim/AnimationClip.h"
#include "anim/SkeletalAssets.h"
#include "gameplay/Actor.h"
#include "gameplay/Appearance.h"
#include "gameplay/AppearanceRegistry.h"
#include "gameplay/AuthoredCharacter.h"
#include "hair/HairRegistry.h"
#include "ui/AppearanceEditor.h"
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

// Undo/redo history bound. One snapshot per slider-edit "session"
// (mouse-down -> mouse-up release), so 64 = 64 distinct edits
// reachable via Ctrl+Z. A full Appearance snapshot is small
// (~couple hundred bytes + the morph-weight map ~2KB), so the whole
// stack at cap is ~150KB -- not worth being clever about.
constexpr std::size_t kAppearanceHistoryCap = 64;

// Top-level category sections in the order they appear in the rail.
// The slider registry's `category` field on each AppearanceSliderDef
// references one of these. Sliders whose category isn't listed here
// fall into "misc" at the end.
//
// The order is sculpting-flow oriented: identity first (the discrete
// body-type pick that gates everything else), then face-down-to-mouth
// in viewing order, then whole-body proportions, then hue last (it's
// a tweak, not a sculpt).
// Category list + framing table + slider blocks live in
// ui/AppearanceEditor -- shared with the Effigie standalone designer.

struct State
{
    bool active = false;
    bool confirmed_this_frame = false;
    char name_buf[kNameMaxLen + 1] = "";
    selva::gameplay::Appearance app;

    // Currently-selected rail category. Drives which sliders the
    // middle pane shows + (later) which camera framing snaps in.
    // Defaults to the first entry on screen open.
    std::string selected_category = selva::ui::appearanceCategories()[0].id;

    // Undo/redo. history[history_index] is the CURRENT state; entries
    // before it are undo targets, entries after are redo targets.
    // pushHistory() truncates anything past the current index (the
    // standard undo-stack "make a change after undoing" semantic --
    // the abandoned future gets dropped).
    std::vector<selva::gameplay::Appearance> history;
    std::size_t history_index = 0;
};

State& state()
{
    static State s;
    return s;
}

// Push the current Appearance onto the undo stack, dropping any redo
// future. Called on slider-release (IsItemDeactivatedAfterEdit) +
// after the BodyType radio toggle -- one stack entry per discrete
// edit, never per pixel of drag.
void pushHistory(State& s)
{
    // Drop any redo future.
    if (s.history_index + 1 < s.history.size())
        s.history.resize(s.history_index + 1);
    s.history.push_back(s.app);
    if (s.history.size() > kAppearanceHistoryCap)
    {
        s.history.erase(s.history.begin());
        // history_index stays pointing at the new "end" after the pop.
    }
    s.history_index = s.history.size() - 1;
}

bool canUndo(const State& s)
{
    return s.history_index > 0;
}
bool canRedo(const State& s)
{
    return s.history_index + 1 < s.history.size();
}

void undo(State& s)
{
    if (!canUndo(s))
        return;
    --s.history_index;
    s.app = s.history[s.history_index];
}

void redo(State& s)
{
    if (!canRedo(s))
        return;
    ++s.history_index;
    s.app = s.history[s.history_index];
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

// Slider blocks, category framings, and helpers moved to
// ui/AppearanceEditor -- shared with the Effigie designer.

// Render the "Title:" label. Caller positions the cursor first so the
// label sits where it should -- typically all-the-way-left of the
// title row to give the field a strong leading anchor.
void drawNameLabel()
{
    ImGui::SetWindowFontScale(1.3f);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Title:");
    ImGui::SetWindowFontScale(1.0f);
}

// Render just the name input -- no label. Caller positions the cursor
// to align the input with the slider column's left edge. Width is
// derived from the slider column so the input visually spans the
// same width as a typical slider+label pair.
void drawNameInput(char* buf, std::size_t buf_size, float input_width)
{
    ImGui::PushItemWidth(input_width);
    ImGui::SetWindowFontScale(1.1f);
    ImGui::InputText("##creator-name", buf, buf_size);
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopItemWidth();
}

// Render just the preview image, centered horizontally inside the
// available child-window area. Surrounding chrome (orbit hints,
// undo/redo buttons) is rendered by the caller so it can be grouped
// or positioned independently. Returns the image's actual width +
// height in pixels for the caller's centering math.
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
    const float pane_w = ImGui::GetContentRegionAvail().x;
    const float img_w = static_cast<float>(w);
    if (pane_w > img_w)
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (pane_w - img_w) * 0.5f);

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
    // Zero the button's Normal / Hovered / Active background colors so
    // the button chrome (default blue-tinted hover overlay) doesn't
    // bleed through transparent regions of the preview texture. Hair
    // meshes carry alpha, so the ImGui hover tint would show as a blue
    // outline wherever the hair silhouette has feathered edges. The
    // ImageButton still fires clicks + drag events for camera orbit;
    // we just want it visually invisible when hovered.
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0));
    ImGui::ImageButton(
        "##creator-preview-image", static_cast<ImTextureID>(static_cast<std::uintptr_t>(tex)),
        ImVec2(img_w, static_cast<float>(h)), ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
    ImGui::PopStyleColor(3);
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
        const float dx_deg = (d.x / img_w) * 360.0f;
        const float dy_deg = (d.y / static_cast<float>(h)) * 180.0f;
        selva::ui::addCharacterPreviewYaw(dx_deg);
        selva::ui::addCharacterPreviewPitch(dy_deg);
        ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
    }
}

// Three-line orbit-control hint block (drag / zoom / reset). Rendered
// where the caller decides; no positioning assumed.
void drawPreviewHints()
{
    ImGui::TextDisabled("Left-drag  -  Rotate");
    ImGui::TextDisabled("Scroll  -  Zoom");
    ImGui::TextDisabled("Right-click  -  Reset");
}

// Undo / Redo button pair. Disabled state reflects history bounds.
// Caller positions; this just emits the two buttons inline.
void drawUndoRedoButtons(State& s)
{
    constexpr float kBtnW = 72.0f;
    const bool ru = canUndo(s);
    const bool rr = canRedo(s);
    if (!ru)
        ImGui::BeginDisabled();
    if (ImGui::Button("Undo##creator", ImVec2(kBtnW, 0.0f)))
        undo(s);
    if (!ru)
        ImGui::EndDisabled();
    ImGui::SameLine();
    if (!rr)
        ImGui::BeginDisabled();
    if (ImGui::Button("Redo##creator", ImVec2(kBtnW, 0.0f)))
        redo(s);
    if (!rr)
        ImGui::EndDisabled();
}

// Lowercase + replace non-[a-z0-9_] with '_' so a character named
// "Alex" gets a sensible "alex" path, "BÆR" gets "b__r", etc.
// Used to derive a per-character character_path. Length capped
// at 6 (matches kNameMaxLen) so the path is bounded.
std::string sanitizeNameForPath(const std::string& name)
{
    std::string out;
    out.reserve(name.size());
    for (const char c : name)
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
// (placeholder) PlayerProfile. Atomic: name + character_path +
// character file all land in the SAME call, no intermediate
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
    p.character_path = "config/characters/" + safe + ".json";
    // Wrap the runtime creator's Appearance into a bare
    // AuthoredCharacter (no identity slice -- the runtime creator
    // sets neither class nor stats; those come from the New Game
    // flow's class-picker downstream). Any identity fields left off
    // land as has_* = false so the on-disk file is appearance-only.
    selva::gameplay::AuthoredCharacter c;
    c.appearance = s.app;
    selva::gameplay::saveAuthoredCharacter(p.character_path, c);
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
    // Default hair: first entry in the "short" bucket (nominally Short 1).
    // Picking a concrete default rather than starting bald keeps the
    // creator preview from opening with a jarring no-hair look; the
    // player can pick bald or a different style immediately.
    for (const auto& style : selva::hair::hairRegistry().styles)
    {
        if (style.length == "short")
        {
            s.app.hair_style_id = style.id;
            break;
        }
    }
    s.selected_category = selva::ui::appearanceCategories()[0].id;
    // Seed the undo stack with the freshly-opened state so the very
    // first edit has something to revert TO. Without this, the first
    // Ctrl+Z after a single slider drag would be a no-op (we'd be at
    // history_index=0 with one entry and canUndo would be false).
    s.history.clear();
    s.history.push_back(s.app);
    s.history_index = 0;
    // Apply the default category's framing so the preview opens at
    // the right zoom/look-at instead of whatever the previous
    // session left lying around.
    if (const auto* f = selva::ui::findAppearanceCategoryFraming(s.selected_category))
        selva::ui::snapCharacterPreviewFraming(f->yaw_degrees, f->pitch_degrees, f->zoom,
                                               f->look_at_fraction);
    selva::gameState().phase = selva::GameState::Phase::CharacterCreation;
}

// Centered confirm button ("Take this form") + name-collision hint,
// with matching disabled visual when the name field can't commit.
// Pulled out of the render function to keep it under the cognitive-
// complexity threshold and because it's a self-contained block.
static void drawCreatorFooter(State& s, bool name_ok, const std::string& trimmed)
{
    constexpr float kFooterRowHeight = 64.0f;
    constexpr float kButtonW = 240.0f;
    constexpr float kHintW = 200.0f;
    ImGui::BeginChild("##creator-footer", ImVec2(0, kFooterRowHeight), false);
    ImGui::Spacing();
    const float footer_w = ImGui::GetContentRegionAvail().x;
    if (footer_w > kButtonW)
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (footer_w - kButtonW) * 0.5f);
    if (!name_ok)
        ImGui::BeginDisabled();
    if (ImGui::Button("Take this form", ImVec2(kButtonW, 36.0f)) && commitCreation(s))
    {
        s.active = false;
        s.confirmed_this_frame = true;
    }
    if (!name_ok)
        ImGui::EndDisabled();
    if (!name_ok && !trimmed.empty())
    {
        if (footer_w > kHintW)
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (footer_w - kHintW) * 0.5f);
        ImGui::TextDisabled("(that name is already borne)");
    }
    ImGui::EndChild();
}

// The creator's ImGui path drives the LIVE player Actor's Appearance
// AND the CharacterPreview module: the runtime creator is what edits
// the actual save-target character. Pulled out of the render function
// so the render function stays focused on layout.
static void syncPlayerAndPreviewToCreatorState(State& s)
{
    selva::gameplay::player().appearance = s.app;
    // Body Type radio is part of Appearance but the SKELETON BUNDLE
    // (mesh + clip registry) is resolved through playerSkeletonKey() --
    // a runtime resolver that's only flipped on character load by
    // default. Mirror the flip here when the radio toggles + rebind
    // the player's sampler to the new skeleton+mesh so the bone
    // palette is computed against the right bind pose (otherwise the
    // live preview deforms wrong -- "kangaroo arms" from female mesh
    // with male-bind palette).
    const std::string new_sk = selva::gameplay::bodyTypeSkeletonId(s.app.body_type);
    if (selva::gameplay::player().skeleton_id != new_sk)
    {
        selva::gameplay::player().skeleton_id = new_sk;
        selva::anim::setPlayerSkeletonKey(new_sk.c_str());
        selva::gameplay::player().sampler = selva::anim::createPoseSampler(
            selva::anim::skeletonByKey(new_sk), selva::anim::meshByKey(new_sk));
        if (const auto* idle = selva::anim::idleClip(); idle != nullptr && idle->isLoaded())
            selva::gameplay::player().sampler.update(*idle, 0.0f, 0.0f);
    }

    // Push the creator's in-flight Appearance into the preview
    // module. This is the ONLY link between the creator's edits
    // (state().app) and what the preview renders -- no shared state,
    // no reads of sPlayer inside the preview module. Cheap: struct
    // copy plus a body_type equality check.
    selva::ui::setCharacterPreviewAppearance(s.app);
    // Render the preview FBO from inside the ImGui render path: the
    // gameplay per-frame tick only renders the preview during
    // Phase::Playing, and we're in CharacterCreation. Without this,
    // the FBO ImGui::Image samples is stale (whatever the last
    // Playing frame left). Done BEFORE the layout so the texture
    // is fresh when sampled below.
    selva::ui::renderCharacterPreview();
}

bool renderCharacterCreationScreen()
{
    State& s = state();
    s.confirmed_this_frame = false;
    if (!s.active)
        return false;

    syncPlayerAndPreviewToCreatorState(s);

    // Full-screen modal with a backdrop. Three-row vertical layout:
    //   [title:  Title + name input, indented to align with sliders]
    //   [body:   rail | sliders | preview-and-controls]
    //   [footer: centered "Take this form"]
    // Title + footer stay put across category selection; only the
    // body pane's sliders column changes per-rail-pick. Undo/Redo
    // live in the preview pane footer next to the orbit-hint
    // cluster so the editing-session controls are grouped with the
    // view-the-result controls.
    drawFullScreenBackdrop(0.65f);
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const ImVec2 panel_size(vp->Size.x * 0.92f, vp->Size.y * 0.92f);
    beginCenteredWindow("##creator", panel_size);
    ImGui::Spacing();

    constexpr float kRailColWidth = 180.0f;
    constexpr float kTitleRowHeight = 56.0f;
    constexpr float kFooterRowHeight = 64.0f;
    constexpr float kPanePaddingX = 18.0f;
    constexpr float kPanePaddingY = 10.0f;
    // Slider content width within the slider pane. Leaves room for
    // the longest label ("Chin Prominence") to the right of the
    // slider itself.
    constexpr float kSliderItemWidth = 220.0f;
    // Preview column = exactly the FBO image width + padding on both
    // sides. Sizing this explicitly (instead of letting it fill
    // remaining width) keeps the undo/redo+hints row visually
    // compact instead of stretched across half the screen.
    const float preview_col_width =
        static_cast<float>(selva::ui::characterPreviewWidth()) + kPanePaddingX * 2.0f;

    // Ctrl+Z / Ctrl+Y keyboard shortcuts. Active across the whole
    // creator. The text-input check excludes the name field so typing
    // letters doesn't push undo entries.
    if (!ImGui::GetIO().WantTextInput)
    {
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z))
            undo(s);
        else if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Y) ||
                 ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z))
            redo(s);
    }

    // Confirm gate computed once up front so the footer button + the
    // name-taken hint share the same source of truth.
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
        auto& chars = selva::saveData().characters;
        for (std::size_t i = 0; i + 1 < chars.size(); ++i)
            if (chars[i].name == trimmed)
                name_ok = false;
    }

    // ---- Title row: label all-the-way-left, input indented to
    //                 align with the slider column's left edge ----
    ImGui::BeginChild("##creator-title", ImVec2(0, kTitleRowHeight), false);
    ImGui::SetCursorPos(ImVec2(kPanePaddingX, kPanePaddingY));
    drawNameLabel();
    ImGui::SameLine();
    // Input X = rail column width + slider pane's internal padding,
    // so the input lines up under the leftmost slider edge.
    ImGui::SetCursorPosX(kRailColWidth + kPanePaddingX);
    drawNameInput(s.name_buf, sizeof(s.name_buf), kSliderItemWidth);
    ImGui::EndChild();
    ImGui::Separator();

    // ---- Body row: rail | preview | sliders ----
    // The PREVIEW gets a fixed width (image + padding); the SLIDERS
    // pane absorbs the remainder. Layout order in code is still
    // rail -> sliders -> preview to match the visual L-to-R order,
    // but the slider column expands instead of the preview.
    const float body_h = ImGui::GetContentRegionAvail().y - kFooterRowHeight -
                         ImGui::GetStyle().ItemSpacing.y * 2.0f;
    ImGui::BeginChild("##creator-body", ImVec2(0, body_h), false);

    // Pane 1: category rail (left, fixed width, padding-inset).
    ImGui::BeginChild("##creator-rail", ImVec2(kRailColWidth, 0), false);
    ImGui::SetCursorPos(ImVec2(kPanePaddingX, kPanePaddingY));
    ImGui::BeginGroup();
    ImGui::SetWindowFontScale(1.15f);
    for (std::size_t i = 0; i < selva::ui::appearanceCategoryCount(); ++i)
    {
        const auto& cat = selva::ui::appearanceCategories()[i];
        if (!selva::ui::appearanceCategoryHasContent(cat.id,
                                                     /*include_designer_only=*/false))
            continue;
        const bool selected = (s.selected_category == cat.id);
        if (ImGui::Selectable(cat.label, selected, 0,
                              ImVec2(kRailColWidth - kPanePaddingX * 2.0f, 30.0f)))
        {
            s.selected_category = cat.id;
            // Snap camera to this category's framing. The user can
            // still drag/scroll to adjust afterwards -- the snap is
            // a starting point, not a lock.
            if (const auto* f = selva::ui::findAppearanceCategoryFraming(cat.id))
                selva::ui::snapCharacterPreviewFraming(f->yaw_degrees, f->pitch_degrees, f->zoom,
                                                       f->look_at_fraction);
        }
    }
    ImGui::SetWindowFontScale(1.0f);
    ImGui::EndGroup();
    ImGui::EndChild();

    // Pane 2: sliders (middle, expands to fill remaining width
    // after the rail + preview claim theirs). Slider control width
    // is fixed; the column itself just gives the labels enough room.
    ImGui::SameLine();
    const float body_avail = ImGui::GetContentRegionAvail().x;
    const float slider_col_width = std::max(280.0f, body_avail - preview_col_width);
    ImGui::BeginChild("##creator-sliders", ImVec2(slider_col_width, 0), false);
    ImGui::SetCursorPos(ImVec2(kPanePaddingX, kPanePaddingY));
    ImGui::BeginGroup();
    {
        ImGui::PushItemWidth(kSliderItemWidth);
        bool edit_committed = false;
        selva::ui::drawAppearanceCategoryContent(s.app, s.selected_category,
                                                 /*include_designer_only=*/false, &edit_committed);
        if (edit_committed)
            pushHistory(s);
        ImGui::PopItemWidth();
    }
    ImGui::EndGroup();
    ImGui::EndChild();

    // Pane 3: preview + its control cluster (fixed width = image + pad).
    // Image centered vertically; bottom row holds [Undo Redo | hints].
    ImGui::SameLine();
    ImGui::BeginChild("##creator-preview", ImVec2(preview_col_width, 0), false);
    {
        const float pane_h = ImGui::GetContentRegionAvail().y;
        const float img_h = static_cast<float>(selva::ui::characterPreviewHeight());
        const float hints_h =
            ImGui::GetTextLineHeightWithSpacing() * 3.0f + ImGui::GetStyle().ItemSpacing.y;
        const float content_h = img_h + hints_h;
        if (pane_h > content_h)
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (pane_h - content_h) * 0.5f);
        drawPreviewImage();
        ImGui::Spacing();

        // Below-image control row: undo/redo on the LEFT, orbit
        // hints on the RIGHT. The preview pane is just-wide-enough
        // for the image, so the two clusters sit comfortably close
        // (no more big stretched gap).
        const float row_y = ImGui::GetCursorPosY();
        ImGui::BeginGroup();
        drawUndoRedoButtons(s);
        ImGui::EndGroup();
        const float hint_w = std::max({ImGui::CalcTextSize("Left-drag  -  Rotate").x,
                                       ImGui::CalcTextSize("Scroll  -  Zoom").x,
                                       ImGui::CalcTextSize("Right-click  -  Reset").x});
        ImGui::SameLine();
        const float right_edge_x = ImGui::GetWindowContentRegionMax().x - hint_w;
        if (right_edge_x > ImGui::GetCursorPosX())
            ImGui::SetCursorPosX(right_edge_x);
        ImGui::SetCursorPosY(row_y);
        ImGui::BeginGroup();
        drawPreviewHints();
        ImGui::EndGroup();
    }
    ImGui::EndChild();

    ImGui::EndChild(); // ##creator-body
    ImGui::Separator();

    drawCreatorFooter(s, name_ok, trimmed);

    ImGui::End();

    return s.confirmed_this_frame;
}

} // namespace selva::ui
