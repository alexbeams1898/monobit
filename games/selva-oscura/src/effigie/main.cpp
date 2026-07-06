// The Effigie -- standalone character designer application.
//
// Same engine, same assets, same Appearance struct as the game. NO
// world, NO gameplay tick, NO physics, NO audio, NO region, NO
// dialog, NO combat data. Boots straight into a preview-only window
// showing the humanoid figure with a designer surface (rail +
// sliders + preview).
//
// Doctrinal shape: the tool authors AuthoredCharacter values that
// the game consumes at runtime. Standalone binary rather than an
// in-game panel so gameplay state (sPlayer, region, save profile)
// can never leak into the authoring flow.
//
// Phases (see design chat 2026-07-01):
//   Phase 1 [SHIPPED]: empty shell that boots + shows preview
//   Phase 2 [SHIPPED]: designer UI (rail | sliders | preview)
//   Phase 4 [SHIPPED]: load/save (file dropdown, save-as)
//   AuthoredCharacter [HERE]: identity slice (name, class, stats, hands)
//   Phase 3:            populate sliders.json + designer-only toggle
//   Phase 5:            pose picker (dropdown to swap idle clip)

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "AppState.h"
#include "Engine.h"
#include "anim/SkeletalAssets.h"
#include "gameplay/Appearance.h"
#include "gameplay/AppearanceRegistry.h"
#include "gameplay/AuthoredCharacter.h"
#include "hair/HairRegistry.h"
#include "lang/Language.h"
#include "ui/AppearanceEditor.h"
#include "ui/CharacterPreview.h"

// SDL provides its own main() on WIN32 unless we opt out. The engine
// exposes its own Engine::run() event loop so we want plain main().
#define SDL_MAIN_HANDLED
#include <imgui.h>

#include <SDL.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace
{

// Designer state. Parallel to the runtime creator's State struct but
// scoped to the Effigie tool -- no name field, no wake-scene, no
// commit-to-savefile. Undo/redo mirror the runtime creator so slider
// drags share the same feel.
//
// Phase 2 stops at Appearance-only editing. Phase 4 grows this to
// AuthoredCharacter (appearance + stats + equipment + class + name)
// once that struct exists.
struct State
{
    // The authored record: appearance + identity slice (name, class,
    // stats, hand items). Snapshotted whole into the undo stack so
    // Ctrl+Z reverts both slider drags AND identity edits.
    selva::gameplay::AuthoredCharacter authored;
    std::string selected_category;

    std::vector<selva::gameplay::AuthoredCharacter> history;
    std::size_t history_index = 0;

    // Phase 3 wires this to a checkbox in the top strip. Today the
    // Effigie starts with the designer-only surface active (all
    // registered sliders visible). Once sliders.json is populated,
    // this is what distinguishes the tool from the runtime creator.
    bool include_designer_only = true;

    // Load/save state. `current_file_stem` is the name (no
    // extension) of the file currently loaded — empty when nothing
    // is loaded and we're on a fresh default AuthoredCharacter. The
    // save-as buffer is what the user types; it seeds from
    // current_file_stem whenever a file is loaded.
    std::string current_file_stem;
    std::vector<std::string> available_files;
    char save_as_buffer[128] = {0};
    std::string last_status;

    // Character-category text-input buffers. Separate from the
    // AuthoredCharacter itself so the input widget can push edits
    // through the sanitizer / trimmer only when the user commits.
    char display_name_key_buffer[64] = {0};
    char rh_item_buffer[64] = {0};
    char lh_item_buffer[64] = {0};
};

State sState;

// Where authored character files live -- the game reads these at
// boot, so the Effigie writes DIRECTLY there rather than into the
// per-target overlay under build/bin/selva-effigie/. That overlay is
// derived data and gets rebuilt every time the CMake sync step runs.
//
// Anchored to the exe location (not CWD) via resolveCharacterDir().
// The exe lives at <repo>/build/bin/selva-effigie/, so we go up FOUR
// SDL_GetBasePath returns with a trailing slash on Windows; the first
// parent_path() strips that trailing separator, so we need
// selva-effigie -> bin -> build -> <repo> = four hops.
std::string sCharacterDir;

// The registry file also lives in that directory but is not itself an
// appearance -- filter it out of the file dropdown.
constexpr const char* kSlidersFilename = "sliders.json";

// The rail entry for the Effigie's identity slice (display_name,
// player_class, stats, rh_item, lh_item). Only exposed by the Effigie
// -- the runtime creator draws Appearance sliders only, since the
// runtime name / class come from the New Game flow. Using an id
// distinct from the shared appearance categories so
// drawAppearanceCategoryContent never receives this string.
constexpr const char* kCharacterCategoryId = "character";
constexpr const char* kCharacterCategoryLabel = "Character";

std::string resolveCharacterDir()
{
    char* base = SDL_GetBasePath();
    if (!base)
        return "config/characters";
    const std::filesystem::path exe_dir(base);
    SDL_free(base);
    // SDL_GetBasePath returns with a trailing slash; on Windows, the
    // first parent_path() strips the trailing separator (converting
    // "C:/.../selva-effigie/" -> "C:/.../selva-effigie"). We then
    // need three more hops (selva-effigie -> bin -> build -> repo).
    const std::filesystem::path repo = exe_dir
                                           .parent_path()  // strip trailing /
                                           .parent_path()  // selva-effigie -> bin
                                           .parent_path()  // bin -> build
                                           .parent_path(); // build -> repo root
    return (repo / "games" / "selva-oscura" / "config" / "characters").string();
}

std::string sanitizeStem(const std::string& raw)
{
    // Strip trailing .json if the user typed it, then keep only
    // characters that survive as an on-disk filename component. Space
    // is allowed; slashes / colons / etc. are not.
    std::string s = raw;
    if (s.size() >= 5)
    {
        const std::string tail = s.substr(s.size() - 5);
        std::string tail_lower;
        tail_lower.reserve(5);
        for (const char c : tail)
            tail_lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        if (tail_lower == ".json")
            s.resize(s.size() - 5);
    }
    std::string out;
    out.reserve(s.size());
    for (const char c : s)
    {
        const unsigned char u = static_cast<unsigned char>(c);
        if (std::isalnum(u) || c == '_' || c == '-' || c == ' ' || c == '.')
            out.push_back(c);
    }
    // Trim trailing / leading whitespace so " foo " -> "foo".
    while (!out.empty() && out.back() == ' ')
        out.pop_back();
    std::size_t lead = 0;
    while (lead < out.size() && out[lead] == ' ')
        ++lead;
    if (lead > 0)
        out.erase(0, lead);
    return out;
}

std::string characterFilePath(const std::string& stem)
{
    return sCharacterDir + "/" + stem + ".json";
}

void refreshAvailableFiles()
{
    sState.available_files.clear();
    std::error_code ec;
    const std::filesystem::directory_iterator it(sCharacterDir, ec);
    if (ec)
    {
        std::fprintf(stderr, "[effigie] cannot scan %s: %s\n", sCharacterDir.c_str(),
                     ec.message().c_str());
        return;
    }
    for (const auto& entry : it)
    {
        if (!entry.is_regular_file())
            continue;
        const auto& p = entry.path();
        if (p.extension() != ".json")
            continue;
        const std::string fname = p.filename().string();
        if (fname == kSlidersFilename)
            continue;
        sState.available_files.push_back(p.stem().string());
    }
    std::sort(sState.available_files.begin(), sState.available_files.end());
}

void seedHistoryFromCurrent()
{
    sState.history.clear();
    sState.history.push_back(sState.authored);
    sState.history_index = 0;
}

// Sync the char-buffer inputs (display_name_key / rh_item / lh_item)
// from the authored record after a load. The buffers are the UI's
// edit scratch space; the record is the truth.
void syncInputBuffersFromAuthored()
{
    std::snprintf(sState.display_name_key_buffer, sizeof(sState.display_name_key_buffer), "%s",
                  sState.authored.display_name_key.c_str());
    std::snprintf(sState.rh_item_buffer, sizeof(sState.rh_item_buffer), "%s",
                  sState.authored.rh_item.c_str());
    std::snprintf(sState.lh_item_buffer, sizeof(sState.lh_item_buffer), "%s",
                  sState.authored.lh_item.c_str());
}

// Draw a "will-apply" checkbox before an identity block. When
// checked, this file's version of that block will overlay onto the
// spawned Actor at load time; when unchecked, the block is omitted
// from the on-disk JSON and the Actor keeps archetype defaults.
// Editing any field inside the block auto-flips the flag ON so
// authors don't have to remember to tick it first.
bool drawAppliesCheckbox(const char* label, bool* flag_ptr, bool* edit_committed_out)
{
    if (ImGui::Checkbox(label, flag_ptr) && edit_committed_out != nullptr)
        *edit_committed_out = true;
    return *flag_ptr;
}

// Name is a LANG-MAP KEY, not a literal. The engine resolves this
// through selva::lang::resolve at every render, so insight tiers
// ("???" -> "Guide") can swap the displayed name without touching the
// authored file. Field label + hint make this unambiguous; the
// resolved-value preview below lets the author confirm the key matches
// something in config/lang/*.json.
void drawNameKeyBlock(bool* edit_committed_out)
{
    if (ImGui::InputText("##character-display-name-key", sState.display_name_key_buffer,
                         sizeof(sState.display_name_key_buffer)))
    {
        sState.authored.display_name_key = sState.display_name_key_buffer;
        sState.authored.has_display_name_key = true;
    }
    if (ImGui::IsItemDeactivatedAfterEdit() && edit_committed_out != nullptr)
        *edit_committed_out = true;
    if (sState.authored.display_name_key.empty())
    {
        ImGui::TextDisabled("(e.g. interact.npc.guide.display_name)");
    }
    else
    {
        const std::string& resolved = selva::lang::resolve(sState.authored.display_name_key);
        ImGui::TextDisabled("resolves to: %s", resolved.c_str());
    }
}

void drawClassBlock(bool* edit_committed_out)
{
    const char* current_class_name = selva::playerClassName(sState.authored.player_class);
    if (!ImGui::BeginCombo("##character-class", current_class_name))
        return;
    for (int i = 0; i <= static_cast<int>(selva::PlayerClass::Unburdened); ++i)
    {
        const auto cls = static_cast<selva::PlayerClass>(i);
        const char* name = selva::playerClassName(cls);
        const bool selected = (sState.authored.player_class == cls);
        if (ImGui::Selectable(name, selected) && !selected)
        {
            sState.authored.player_class = cls;
            sState.authored.has_player_class = true;
            if (edit_committed_out != nullptr)
                *edit_committed_out = true;
        }
        if (selected)
            ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
}

// Stat range 1..99 mirrors Souls-lineage convention. Base of 1 is the
// struct default; 99 is the practical soft-cap ceiling.
void drawStatsBlock(bool* edit_committed_out)
{
    struct StatDef
    {
        const char* label;
        int selva::gameplay::Stats::*ptr;
    };
    static constexpr StatDef kStats[] = {
        {"STR##character-stat", &selva::gameplay::Stats::str},
        {"DEX##character-stat", &selva::gameplay::Stats::dex},
        {"END##character-stat", &selva::gameplay::Stats::end},
        {"LCK##character-stat", &selva::gameplay::Stats::lck},
        {"PER##character-stat", &selva::gameplay::Stats::per},
        {"COG##character-stat", &selva::gameplay::Stats::cog},
        {"INT##character-stat", &selva::gameplay::Stats::intl},
    };
    for (const auto& sd : kStats)
    {
        if (ImGui::SliderInt(sd.label, &(sState.authored.stats.*(sd.ptr)), 1, 99))
            sState.authored.has_stats = true;
        if (ImGui::IsItemDeactivatedAfterEdit() && edit_committed_out != nullptr)
            *edit_committed_out = true;
    }
}

// One hand-item text input. `slot_label` is "RH" or "LH"; `buffer` is
// the input's edit scratch (staged separately from the AuthoredChar
// so IsItemDeactivatedAfterEdit reports intent-to-commit cleanly);
// `authored_field` / `authored_has` mutate the record on commit.
void drawHandBlock(const char* slot_label, char* buffer, std::size_t buffer_size,
                   std::string& authored_field, bool& authored_has,
                   bool* edit_committed_out)
{
    const std::string id = std::string(slot_label) + "##character-hand-" + slot_label;
    if (ImGui::InputText(id.c_str(), buffer, buffer_size))
    {
        authored_field = buffer;
        authored_has = true;
    }
    if (ImGui::IsItemDeactivatedAfterEdit() && edit_committed_out != nullptr)
        *edit_committed_out = true;
}

// Render the identity slice: display_name, PlayerClass combo, seven
// stat sliders, rh_item / lh_item text inputs. Effigie-only -- the
// runtime creator doesn't touch these because in-game name / class
// come from the New Game flow, not the appearance sliders.
//
// Each block sits behind an "applies" checkbox bound to the
// AuthoredCharacter's has_* flag. Editing a field inside the block
// auto-opts-in (flips the flag on). Uncheck to omit the block from
// the on-disk file entirely -- appearance-only files stay lean.
void drawCharacterCategory(bool* edit_committed_out)
{
    if (drawAppliesCheckbox("Name key", &sState.authored.has_display_name_key,
                            edit_committed_out))
        drawNameKeyBlock(edit_committed_out);
    ImGui::Spacing();
    if (drawAppliesCheckbox("Class", &sState.authored.has_player_class, edit_committed_out))
        drawClassBlock(edit_committed_out);
    ImGui::Spacing();
    if (drawAppliesCheckbox("Stats", &sState.authored.has_stats, edit_committed_out))
        drawStatsBlock(edit_committed_out);
    ImGui::Spacing();
    if (drawAppliesCheckbox("Right hand", &sState.authored.has_rh_item, edit_committed_out))
        drawHandBlock("RH", sState.rh_item_buffer, sizeof(sState.rh_item_buffer),
                       sState.authored.rh_item, sState.authored.has_rh_item,
                       edit_committed_out);
    ImGui::Spacing();
    if (drawAppliesCheckbox("Left hand", &sState.authored.has_lh_item, edit_committed_out))
        drawHandBlock("LH", sState.lh_item_buffer, sizeof(sState.lh_item_buffer),
                       sState.authored.lh_item, sState.authored.has_lh_item,
                       edit_committed_out);
}

void loadFileIntoState(const std::string& stem)
{
    const std::string path = characterFilePath(stem);
    sState.authored = selva::gameplay::loadAuthoredCharacter(path);
    sState.current_file_stem = stem;
    std::snprintf(sState.save_as_buffer, sizeof(sState.save_as_buffer), "%s", stem.c_str());
    syncInputBuffersFromAuthored();
    seedHistoryFromCurrent();
    sState.last_status = std::string("Loaded ") + stem + ".json";
    std::fprintf(stderr, "[effigie] loaded %s\n", path.c_str());
}

void saveCurrentAppearanceAs(const std::string& stem)
{
    const std::string path = characterFilePath(stem);
    std::error_code ec;
    std::filesystem::create_directories(sCharacterDir, ec);
    const bool ok = selva::gameplay::saveAuthoredCharacter(path, sState.authored);
    if (!ok)
    {
        sState.last_status = std::string("SAVE FAILED: ") + path;
        std::fprintf(stderr, "[effigie] save failed: %s\n", path.c_str());
        return;
    }
    sState.current_file_stem = stem;
    sState.last_status = std::string("Saved ") + stem + ".json";
    std::fprintf(stderr, "[effigie] saved %s\n", path.c_str());
    refreshAvailableFiles();
}

void pushHistory()
{
    if (sState.history_index + 1 < sState.history.size())
        sState.history.resize(sState.history_index + 1);
    sState.history.push_back(sState.authored);
    sState.history_index = sState.history.size() - 1;
}

bool canUndo()
{
    return sState.history_index > 0;
}
bool canRedo()
{
    return sState.history_index + 1 < sState.history.size();
}
void undo()
{
    if (canUndo())
    {
        sState.authored = sState.history[--sState.history_index];
        syncInputBuffersFromAuthored();
    }
}
void redo()
{
    if (canRedo())
    {
        sState.authored = sState.history[++sState.history_index];
        syncInputBuffersFromAuthored();
    }
}

void redirectStdioToLog()
{
    (void)std::freopen("effigie.log", "w", stdout);
    (void)std::freopen("effigie.log", "a", stderr);
    std::setvbuf(stderr, nullptr, _IOLBF, 4096);
    std::setvbuf(stdout, nullptr, _IOLBF, 4096);
}

void effigiePerFrame(::Engine& /*engine*/, ::EntityManager& /*em*/, double /*dt*/)
{
    // Push the in-memory Appearance into the preview module. Same
    // API the runtime creator and F1 tuning panel use.
    selva::ui::setCharacterPreviewAppearance(sState.authored.appearance);
    selva::ui::renderCharacterPreview();
}

void drawUndoRedoRow()
{
    if (!canUndo())
        ImGui::BeginDisabled();
    if (ImGui::Button("Undo##effigie", ImVec2(80.0f, 0.0f)))
        undo();
    if (!canUndo())
        ImGui::EndDisabled();
    ImGui::SameLine();
    if (!canRedo())
        ImGui::BeginDisabled();
    if (ImGui::Button("Redo##effigie", ImVec2(80.0f, 0.0f)))
        redo();
    if (!canRedo())
        ImGui::EndDisabled();
}

void drawPreviewImage(float img_w, float img_h)
{
    const unsigned int tex = selva::ui::characterPreviewTexture();
    if (tex == 0)
        return;
    // Zero button padding so the visible image is the FBO size, and
    // suppress button background so a hover doesn't tint the preview.
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0));
    ImGui::ImageButton("##effigie-preview-image",
                       static_cast<ImTextureID>(static_cast<std::uintptr_t>(tex)),
                       ImVec2(img_w, img_h), ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();

    // Camera orbit: left-drag rotates, scroll zooms, right-click resets.
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
        const float dy_deg = (d.y / img_h) * 180.0f;
        selva::ui::addCharacterPreviewYaw(dx_deg);
        selva::ui::addCharacterPreviewPitch(dy_deg);
        ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
    }
}

// Title | file dropdown | save-as input | Save button | status | undo/redo.
// Pulled out of effigieRenderImGui so the main render function stays
// under the cognitive-complexity budget.
void drawTopStrip(float padding_x, float padding_y, float height)
{
    ImGui::BeginChild("##effigie-top", ImVec2(0, height), false);
    ImGui::SetCursorPos(ImVec2(padding_x, padding_y));
    ImGui::TextUnformatted("The Effigie");

    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 24.0f);
    ImGui::TextUnformatted("File:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(220.0f);
    const char* combo_preview =
        sState.current_file_stem.empty() ? "(new)" : sState.current_file_stem.c_str();
    if (ImGui::BeginCombo("##effigie-file-combo", combo_preview))
    {
        // Reflect any writes the user just made outside the tool.
        refreshAvailableFiles();
        for (const auto& stem : sState.available_files)
        {
            const bool is_selected = (stem == sState.current_file_stem);
            if (ImGui::Selectable(stem.c_str(), is_selected))
                loadFileIntoState(stem);
            if (is_selected)
                ImGui::SetItemDefaultFocus();
        }
        if (sState.available_files.empty())
            ImGui::TextDisabled("(no files)");
        ImGui::EndCombo();
    }

    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 12.0f);
    ImGui::TextUnformatted("Save as:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(220.0f);
    ImGui::InputText("##effigie-save-as", sState.save_as_buffer, sizeof(sState.save_as_buffer));

    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8.0f);
    const std::string sanitized_stem = sanitizeStem(sState.save_as_buffer);
    const bool save_enabled = !sanitized_stem.empty();
    if (!save_enabled)
        ImGui::BeginDisabled();
    if (ImGui::Button("Save##effigie", ImVec2(80.0f, 0.0f)))
        saveCurrentAppearanceAs(sanitized_stem);
    if (!save_enabled)
        ImGui::EndDisabled();

    // Status toast between Save and Undo/Redo.
    if (!sState.last_status.empty())
    {
        ImGui::SameLine();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 12.0f);
        ImGui::TextDisabled("%s", sState.last_status.c_str());
    }

    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - 176.0f);
    drawUndoRedoRow();

    ImGui::EndChild();
    ImGui::Separator();
}

void effigieRenderImGui(::Engine& /*engine*/, ::EntityManager& /*em*/)
{
    // Ctrl+Z / Ctrl+Y anywhere in the window.
    if (!ImGui::GetIO().WantTextInput)
    {
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z))
            undo();
        else if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Y) ||
                 ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z))
            redo();
    }

    // Full-window root panel: [top strip | body: rail | sliders | preview]
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    constexpr ImGuiWindowFlags kRootFlags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_NoSavedSettings;
    ImGui::Begin("##effigie-root", nullptr, kRootFlags);

    constexpr float kRailColWidth = 200.0f;
    constexpr float kPanePaddingX = 18.0f;
    constexpr float kPanePaddingY = 10.0f;
    constexpr float kSliderItemWidth = 220.0f;
    constexpr float kTopStripHeight = 44.0f;
    const float preview_col_width =
        static_cast<float>(selva::ui::characterPreviewWidth()) + kPanePaddingX * 2.0f;

    drawTopStrip(kPanePaddingX, kPanePaddingY, kTopStripHeight);

    // ---- Body: rail | sliders | preview ----
    const float body_h = ImGui::GetContentRegionAvail().y;
    ImGui::BeginChild("##effigie-body", ImVec2(0, body_h), false);

    // Pane 1: category rail.
    ImGui::BeginChild("##effigie-rail", ImVec2(kRailColWidth, 0), false);
    ImGui::SetCursorPos(ImVec2(kPanePaddingX, kPanePaddingY));
    ImGui::BeginGroup();
    ImGui::SetWindowFontScale(1.15f);
    // Character row first -- Effigie-only. Selecting it doesn't reframe
    // the preview (no natural framing for identity edits), so the
    // camera stays wherever the user last put it.
    {
        const bool selected = (sState.selected_category == kCharacterCategoryId);
        if (ImGui::Selectable(kCharacterCategoryLabel, selected, 0,
                              ImVec2(kRailColWidth - kPanePaddingX * 2.0f, 30.0f)))
            sState.selected_category = kCharacterCategoryId;
    }
    for (std::size_t i = 0; i < selva::ui::appearanceCategoryCount(); ++i)
    {
        const auto& cat = selva::ui::appearanceCategories()[i];
        if (!selva::ui::appearanceCategoryHasContent(cat.id, sState.include_designer_only))
            continue;
        const bool selected = (sState.selected_category == cat.id);
        if (ImGui::Selectable(cat.label, selected, 0,
                              ImVec2(kRailColWidth - kPanePaddingX * 2.0f, 30.0f)))
        {
            sState.selected_category = cat.id;
            if (const auto* f = selva::ui::findAppearanceCategoryFraming(cat.id))
                selva::ui::snapCharacterPreviewFraming(f->yaw_degrees, f->pitch_degrees, f->zoom,
                                                       f->look_at_fraction);
        }
    }
    ImGui::SetWindowFontScale(1.0f);
    ImGui::EndGroup();
    ImGui::EndChild();

    // Pane 2: sliders.
    ImGui::SameLine();
    const float body_avail = ImGui::GetContentRegionAvail().x;
    const float slider_col_width = std::max(320.0f, body_avail - preview_col_width);
    ImGui::BeginChild("##effigie-sliders", ImVec2(slider_col_width, 0), true);
    ImGui::SetCursorPos(ImVec2(kPanePaddingX, kPanePaddingY));
    ImGui::BeginGroup();
    ImGui::PushItemWidth(kSliderItemWidth);
    {
        bool edit_committed = false;
        if (sState.selected_category == kCharacterCategoryId)
            drawCharacterCategory(&edit_committed);
        else
            selva::ui::drawAppearanceCategoryContent(sState.authored.appearance,
                                                     sState.selected_category,
                                                     sState.include_designer_only, &edit_committed);
        if (edit_committed)
            pushHistory();
    }
    ImGui::PopItemWidth();
    ImGui::EndGroup();
    ImGui::EndChild();

    // Pane 3: preview.
    ImGui::SameLine();
    ImGui::BeginChild("##effigie-preview", ImVec2(preview_col_width, 0), false);
    {
        const float pane_h = ImGui::GetContentRegionAvail().y;
        const float img_h_raw = static_cast<float>(selva::ui::characterPreviewHeight());
        const float img_w_raw = static_cast<float>(selva::ui::characterPreviewWidth());
        // Fit-to-height if the pane is shorter than the FBO's native
        // resolution; otherwise center at native size.
        const float scale = std::min(1.0f, pane_h / img_h_raw);
        const float img_w = img_w_raw * scale;
        const float img_h = img_h_raw * scale;
        const float top_pad = std::max(0.0f, (pane_h - img_h) * 0.5f);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + top_pad);
        // Center horizontally within the pane.
        const float pane_w = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX(std::max(0.0f, (pane_w - img_w) * 0.5f));
        drawPreviewImage(img_w, img_h);
    }
    ImGui::EndChild();

    ImGui::EndChild(); // effigie-body
    ImGui::End();      // effigie-root
}

} // namespace

int main(int /*argc*/, char* /*argv*/[])
{
    redirectStdioToLog();
    std::fprintf(stderr, "[effigie] boot\n");
    std::fflush(stderr);

    Engine engine;
    engine.setMSAA(4);
    engine.setWindowMode(Engine::WindowMode::Windowed);
    if (!engine.init("The Effigie", 1600, 1000))
    {
        std::fprintf(stderr, "[effigie] engine init failed\n");
        return 1;
    }
    engine.setClearColor(0.02f, 0.02f, 0.024f);
    SDL_SetRelativeMouseMode(SDL_FALSE);

    if (!selva::anim::initSkeletalAssets())
    {
        std::fprintf(stderr, "[effigie] skeletal assets failed to load -- cannot show preview\n");
        return 1;
    }
    selva::gameplay::AppearanceRegistry::instance().loadFromFile("config/characters/sliders.json");
    selva::hair::loadHairRegistry("config/hair_styles.json");
    // Populate the lang map so the Name-key input can show the
    // resolved value as feedback. Uses the same default dir the game
    // uses; missing dir logs but doesn't fail.
    selva::lang::loadDirectory();
    if (!selva::ui::initCharacterPreview())
    {
        std::fprintf(stderr, "[effigie] character preview init failed\n");
        return 1;
    }

    // Seed the designer with the Character category selected so the
    // author sees identity fields first -- the name / class / stats
    // pane opens the tool. History is seeded from a default-
    // constructed authored record so Ctrl+Z after the first edit
    // reverts to blank.
    sState.selected_category = kCharacterCategoryId;
    sCharacterDir = resolveCharacterDir();
    std::fprintf(stderr, "[effigie] character dir: %s\n", sCharacterDir.c_str());
    seedHistoryFromCurrent();
    refreshAvailableFiles();

    engine.setPerFrameUpdate(&effigiePerFrame);
    engine.setRenderImGui(&effigieRenderImGui);

    std::fprintf(stderr, "[effigie] main loop ready\n");
    std::fflush(stderr);
    engine.run();
    std::_Exit(0);
}
