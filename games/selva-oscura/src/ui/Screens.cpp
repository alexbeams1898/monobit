#include "ui/Screens.h"

#include "AppState.h"
#include "AppStateGlobal.h"
#include "Engine.h"
#include "SaveManager.h"
#include "dialog/DialogSystem.h"
#include "ecs/GameComponents.h"
#include "ecs/ItemConfig.h"
#include "gameplay/Actor.h"
#include "gameplay/BossState.h"
#include "gameplay/PerFrameTick.h"
#include "gameplay/RomanNumeral.h"
#include "gameplay/TickState.h"
#include "insight/Insight.h"
#include "items/CategoryRegistry.h"
#include "items/InventoryOps.h"
#include "items/ItemRegistry.h"
#include "items/UseHandlers.h"
#include "lang/Language.h"
#include "ops/InventoryOps.h"
#include "text/TextPresentation.h"
#include "ui/BossHud.h"
#include "ui/ClassPickerScreen.h"
#include "ui/NamePromptScreen.h"
#include "ui/UIComponents.h"

#include <imgui.h>

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>

// ---------------------------------------------------------------------------
// Screens.cpp - ImGui-driven screen rendering for Selva's main menu, character
// create, load game, settings, and pause overlay.
//
// All screens share the same TU because they're small, ImGui-based, and
// share state through the AppStateGlobal singletons. The single entry point
// renderScreens() dispatches on GameState::phase + UIState. Each screen
// updates GameState/SaveData directly when the user takes an action.
// ---------------------------------------------------------------------------

namespace selva::ui
{

namespace
{

// Set to true on the frame we transition into Playing, so main.cpp can
// switch SDL_SetRelativeMouseMode + capture the mouse for gameplay.
// Cleared after one frame.
bool sJustEnteredPlaying = false;

// Set to true on the frame we leave Playing (pause menu, quit-to-menu),
// so main.cpp can release the mouse.
bool sJustLeftPlaying = false;

// Sets the active phase, releasing or capturing the mouse as appropriate.
// Called by the screens when the user selects an action.
void setPhase(GameState::Phase next)
{
    auto& gs = gameState();
    const bool was_playing = (gs.phase == GameState::Phase::Playing);
    const bool will_be_playing = (next == GameState::Phase::Playing);
    if (was_playing && !will_be_playing)
    {
        sJustLeftPlaying = true;
        // Per-character world/cinematic/sampler state is reset by the
        // next hardResetWorldForCharacter on Playing-enter, not here.
        // setPhase only handles UI state that lives outside the world.
    }
    if (!was_playing && will_be_playing)
    {
        sJustEnteredPlaying = true;
        // BossHud cache is UI state, not world state -- reset here
        // (hardResetWorldForCharacter doesn't own UI caches).
        selva::ui::resetBossHud();
    }
    gs.phase = next;
}

// Window/button/back-gesture/hint-bar helpers live in ui::UIComponents
// now (shared across every screen + the class picker). File-local
// aliases keep this file's call-sites short; remove them after a
// future readability pass once enough call sites point at the
// namespaced versions directly.
using selva::ui::beginCenteredWindow;
using selva::ui::centeredButton;
using selva::ui::drawHintBar;
using selva::ui::rmbClicked;
using selva::ui::wantBack;

// Flush the runtime player's persistent state into the active
// character's PlayerProfile, then write the SaveData to disk.
//
// THIS IS THE SOLE SITE that triggers the "Saving..." indicator. The
// indicator's `uiState().last_save_ticks_ms` field is written exactly
// once per successful character-save here -- never from any other
// call to SaveManager::save. That couples the visible "Saving..."
// chip tightly to actual character-save events; menu-state writes
// (addCharacter, deleteCharacter, settings changes) go through
// SaveManager::save directly and do NOT show the chip because they
// aren't saving the player's RUN.
//
// Unnamed-but-real character: an unnamed run (name=="") is a fully
// real character in saveData; flushAndSave persists it the same as
// any named character. The Vessel tab + Load menu render "???" for
// empty names but the file write is unconditional.
//
// Defensive: if the active profile doesn't resolve (shouldn't
// happen via normal flow), skip -- there's no run to save.
//
// Called by: pause-menu Save button, Quit-to-Main-Menu, pause-menu
// open (Elden-Ring-style autosave). Returns true if a save actually
// happened (caller doesn't read it today, but the bool surfaces
// intent + supports future "save failed?" UX).
bool flushAndSave()
{
    PlayerProfile* target = selva::activePlayerProfile();
    if (target == nullptr)
        return false;
    selva::gameplay::saveActiveCharacterFromPlayer(*target);
    if (!SaveManager::save(saveData()))
        return false;
    uiState().last_save_ticks_ms = SDL_GetTicks64();
    return true;
}

// ---------------------------------------------------------------------------
// Main menu screen
// ---------------------------------------------------------------------------
bool renderMainMenu()
{
    bool quit = false;
    beginCenteredWindow("##mainmenu", ImVec2(360, 320));
    ImGui::SetWindowFontScale(1.6f);
    ImGui::TextUnformatted("SELVA OSCURA");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::Spacing();

    if (centeredButton("New Game"))
    {
        // Unnamed-but-real character pattern. The unnamed character is
        // a PlayerProfile with name=="" living in saveData like any
        // other -- no separate in-memory branch. If an existing
        // name=="" entry is already in saveData (an unfinished prior
        // unnamed run), resume it; otherwise create a fresh one and
        // persist immediately so the save is real from the first
        // frame. The Guide-elicited naming dialog later mutates the
        // name field directly; class picker writes player_class
        // directly. Per the locked design: one unnamed character at
        // a time.
        auto& sd = saveData();
        bool existing_unnamed = false;
        for (const auto& c : sd.characters)
        {
            if (c.name.empty())
            {
                existing_unnamed = true;
                break;
            }
        }
        if (!existing_unnamed)
        {
            SaveManager::addCharacter(sd, "");
            SaveManager::save(sd);
        }
        selva::gameState().active_character.clear(); // "" resolves to the unnamed profile
        selva::gameState().pending_world_create = true;
        selva::gameState().pending_wake_scene = true;
        setPhase(GameState::Phase::Playing);
    }
    ImGui::Spacing();

    const bool has_characters = !saveData().characters.empty();
    if (!has_characters)
        ImGui::BeginDisabled();
    if (centeredButton("Load Game"))
        setPhase(GameState::Phase::LoadGame);
    if (!has_characters)
        ImGui::EndDisabled();
    ImGui::Spacing();

    if (centeredButton("Settings"))
        setPhase(GameState::Phase::Settings);
    ImGui::Spacing();

    if (centeredButton("Quit"))
        quit = true;

    ImGui::End();
    return quit;
}

// ---------------------------------------------------------------------------
// Load game screen
// ---------------------------------------------------------------------------
void renderLoadGame()
{
    beginCenteredWindow("##loadgame", ImVec2(420, 400));
    ImGui::SetWindowFontScale(1.3f);
    ImGui::TextUnformatted("Load Game");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    int delete_index = -1;
    for (std::size_t i = 0; i < saveData().characters.size(); ++i)
    {
        const auto& c = saveData().characters[i];
        ImGui::PushID(static_cast<int>(i));
        // Unnamed characters (name=="") show as '???' in the slot.
        // The lookup-by-name still works because active_character
        // also goes empty for the unnamed run.
        const std::string label = c.name.empty() ? "???" : c.name;
        if (ImGui::Button(label.c_str(), ImVec2(200, 0)))
        {
            gameState().active_character = c.name;
            gameState().pending_world_create = true;
            setPhase(GameState::Phase::Playing);
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete", ImVec2(80, 0)))
            delete_index = static_cast<int>(i);
        ImGui::PopID();
        ImGui::Spacing();
    }

    if (delete_index >= 0)
    {
        const std::string name = saveData().characters[static_cast<std::size_t>(delete_index)].name;
        SaveManager::deleteCharacter(saveData(), name);
        SaveManager::save(saveData());
    }

    ImGui::End();
    drawHintBar("[Esc/RMB] Back", ImVec2(420, 400));

    if (wantBack())
        setPhase(GameState::Phase::MainMenu);
}

// ---------------------------------------------------------------------------
// Settings screen
// ---------------------------------------------------------------------------
void renderSettings()
{
    beginCenteredWindow("##settings", ImVec2(420, 260));
    ImGui::SetWindowFontScale(1.3f);
    ImGui::TextUnformatted("Settings");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    bool dirty = false;
    auto& s = saveData().settings;
    if (ImGui::SliderFloat("BGM volume", &s.bgm_volume, 0.0f, 1.0f, "%.2f"))
        dirty = true;
    if (ImGui::SliderFloat("SFX volume", &s.sfx_volume, 0.0f, 1.0f, "%.2f"))
        dirty = true;
    if (ImGui::SliderFloat("FOV (third person)", &s.fov_degrees_third_person, 40.0f, 110.0f,
                           "%.0f"))
        dirty = true;
    if (ImGui::SliderFloat("FOV (first person)", &s.fov_degrees_first_person, 50.0f, 120.0f,
                           "%.0f"))
        dirty = true;

    if (dirty)
        SaveManager::save(saveData());

    ImGui::End();
    drawHintBar("[Esc/RMB] Back", ImVec2(420, 260));

    if (wantBack())
        setPhase(GameState::Phase::MainMenu);
}

// Renders the System tab's contents: save / settings sliders / quit-to-menu
// / quit-to-desktop. Returns true if Quit-to-Desktop was selected.
bool renderSystemTab()
{
    bool quit = false;
    ImGui::Spacing();

    if (centeredButton("Save", 220.0f))
        flushAndSave();
    ImGui::Spacing();
    ImGui::Spacing();

    ImGui::TextDisabled("Settings");
    ImGui::Separator();
    ImGui::Spacing();
    bool dirty = false;
    auto& s = saveData().settings;
    if (ImGui::SliderFloat("BGM volume", &s.bgm_volume, 0.0f, 1.0f, "%.2f"))
        dirty = true;
    if (ImGui::SliderFloat("SFX volume", &s.sfx_volume, 0.0f, 1.0f, "%.2f"))
        dirty = true;
    if (ImGui::SliderFloat("FOV (third person)", &s.fov_degrees_third_person, 40.0f, 110.0f,
                           "%.0f"))
        dirty = true;
    if (ImGui::SliderFloat("FOV (first person)", &s.fov_degrees_first_person, 50.0f, 120.0f,
                           "%.0f"))
        dirty = true;
    if (dirty)
        SaveManager::save(saveData());

    ImGui::Spacing();
    ImGui::Spacing();
    if (centeredButton("Quit to Main Menu", 220.0f))
    {
        flushAndSave();
        uiState().active_screen = UIState::Screen::None;
        setPhase(GameState::Phase::MainMenu);
    }
    ImGui::Spacing();
    if (centeredButton("Quit to Desktop", 220.0f))
        quit = true;

    return quit;
}

// ---------------------------------------------------------------------------
// Pause menu overlay
// ---------------------------------------------------------------------------
namespace
{
// Render a single sangue-ledger row: a label string + the value as
// Roman numerals (with single/double vinculum bars where the value
// crosses thousands/millions). 0 still renders an explicit glyph row
// rather than blank space, so the page reads as "this ledger exists
// but holds none" rather than "this ledger is absent."
void drawSangueLedgerRow(const char* label, std::uint32_t amount)
{
    ImGui::Text("%s", label);
    ImGui::SameLine();
    const auto rendering = selva::gameplay::encodeRoman(amount);
    if (rendering.glyphs.empty())
    {
        ImGui::TextDisabled(" (none)");
        return;
    }
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    selva::ui::drawRomanGlyphs(draw, pos, rendering, IM_COL32(220, 200, 180, 255));
    // Advance ImGui's layout past the glyph row so subsequent items
    // don't overlap. Approximate width = glyph count * font width;
    // height = one font line.
    const float glyph_w = ImGui::GetFontSize() * 0.6f;
    ImGui::Dummy(
        ImVec2(glyph_w * static_cast<float>(rendering.glyphs.size()), ImGui::GetFontSize() * 1.1f));
}

void renderPauseVesselOverview()
{
    ImGui::Spacing();
    // Character name -- '???' until the Guide elicits one in dialog.
    ImGui::Text("Name: %s", selva::activeCharacterDisplayName().c_str());
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    // Three substance ledgers, each row gated on its own insight node
    // so the Vagrant only sees ledgers for things he has the conceptual
    // frame for. Labels are tier-0 only (the Vagrant's operational
    // voice) -- the Guide is not a privileged-vocabulary source, so
    // there's no cosmological tier-promotion path on these labels.
    const PlayerProfile* profile = selva::activePlayerProfile();
    const std::uint32_t vessel = (profile != nullptr) ? profile->sangue_vessel : 0u;
    const std::uint32_t riversato = (profile != nullptr) ? profile->sangue_riversato : 0u;
    const std::uint32_t lifetime = (profile != nullptr) ? profile->sangue_lifetime : 0u;
    bool any_row_shown = false;
    if (selva::hasInsight("knows_substance_taken"))
    {
        drawSangueLedgerRow(selva::lang::resolve("vessel.overview.held.label").c_str(), vessel);
        any_row_shown = true;
    }
    if (selva::hasInsight("knows_substance_committed"))
    {
        drawSangueLedgerRow(selva::lang::resolve("vessel.overview.committed.label").c_str(),
                            riversato);
        any_row_shown = true;
    }
    if (selva::hasInsight("knows_substance_lifetime"))
    {
        drawSangueLedgerRow(selva::lang::resolve("vessel.overview.lifetime.label").c_str(),
                            lifetime);
        any_row_shown = true;
    }
    if (!any_row_shown)
        ImGui::TextDisabled("(nothing yet)");
}

void renderPauseVesselForm()
{
    ImGui::Spacing();
    const auto& p = selva::gameplay::player();
    ImGui::Text("HP      %d / %d", p.hp.current, p.hp.max);
    ImGui::Text("Stamina %d / %d", static_cast<int>(std::floor(p.stamina.current)),
                static_cast<int>(std::floor(p.stamina.max)));
    ImGui::Text("Poise   %d / %d", static_cast<int>(std::floor(p.poise.current)),
                static_cast<int>(std::floor(p.poise.max)));
    ImGui::Spacing();
    ImGui::Text("STR %d  DEX %d  END %d  LCK %d", p.stats.str, p.stats.dex, p.stats.end,
                p.stats.lck);
}

void renderPauseVesselHands()
{
    ImGui::Spacing();
    const auto& inv = playerInventory();
    const auto& eq = playerEquipment();
    auto draw_slot = [&](const char* label, EquipSlot slot)
    {
        const std::string path = InventoryOps::equippedPath(inv, eq, slot);
        if (path.empty())
            ImGui::Text("%s: (none)", label);
        else
        {
            const ItemDef* def = itemRegistry().find(path);
            const char* name = (def != nullptr) ? def->name.c_str() : path.c_str();
            ImGui::Text("%s: %s", label, name);
        }
    };
    draw_slot("Right hand", EquipSlot::RightHand);
    draw_slot("Left hand", EquipSlot::LeftHand);
    draw_slot("Head", EquipSlot::Head);
    draw_slot("Chest", EquipSlot::Chest);
    draw_slot("Legs", EquipSlot::Legs);
    draw_slot("Feet", EquipSlot::Feet);
    draw_slot("Accessory 1", EquipSlot::Accessory1);
    draw_slot("Accessory 2", EquipSlot::Accessory2);
}

// Mind sub-page: persistent-canvas workbench + library panel.
//
// The Mind page is the player's deduction surface and self-arranged
// graph of understanding. Per the locked design (2026-06-09 workbench-
// as-designer):
//   - Library lists observations NOT yet on the workbench.
//   - Player drags observations from the library to the workbench at
//     a chosen position. Positions persist across sessions.
//   - Player can drag nodes around the workbench freely; positions
//     auto-save.
//   - Player selects 2+ observations and clicks Deduce to attempt a
//     conclusion. Successful conclusions appear at the centroid of
//     their requires with edges drawn back.
//   - Conclusions LOCK their requires: while a conclusion needs an
//     observation, that observation can't be dragged off the workbench.
//     Drag the conclusion off first to release.
//   - Drag-to-library: a drop zone on the library accepts workbench
//     nodes for return.
//   - Confirmation: select an uncertain conclusion + the confirming
//     observation, click Confirm. The dashed edge appears.

namespace
{

std::string libraryShortLabel(const std::string& id)
{
    const std::string key = "mind." + id + ".summary";
    const std::string& full = selva::lang::resolve(key);
    if (full.empty() || full[0] == '[' || full[0] == '(')
        return id;
    std::size_t period = full.find('.');
    std::size_t cut = (period != std::string::npos && period < 60) ? (period + 1) : 60;
    if (cut >= full.size())
        return full;
    return full.substr(0, cut) + " ...";
}

bool isObservationOnWorkbench(const PlayerProfile& p, const std::string& id)
{
    for (const auto& w : p.workbench_observations)
        if (w.id == id)
            return true;
    return false;
}

bool isConclusionOnWorkbench(const PlayerProfile& p, const std::string& id)
{
    for (const auto& w : p.workbench_inferences)
        if (w.id == id)
            return true;
    return false;
}

// Returns true if any workbench-conclusion has linked (via Deduce or
// Confirm) this observation. Player-act-driven, NOT inferred from the
// node's authored requires. Per the locked design: links exist only
// when the player explicitly created them.
bool observationLockedByConclusion(const PlayerProfile& p, const std::string& obs_id)
{
    for (const auto& conc : p.workbench_inferences)
    {
        if (std::find(conc.linked_observations.begin(), conc.linked_observations.end(), obs_id) !=
            conc.linked_observations.end())
            return true;
        if (std::find(conc.linked_confirmers.begin(), conc.linked_confirmers.end(), obs_id) !=
            conc.linked_confirmers.end())
            return true;
    }
    return false;
}

// Unified selection helpers per the Mind sub-page's MindSelectedItem
// model. The selection set is one vector of (id, location) pairs.
// Buttons derive enable state from these tallies. Clicking in one
// location while items from the other are selected clears the other
// location (mutual exclusion).
bool mindIsSelected(const UIState& ui, const std::string& id)
{
    for (const auto& s : ui.mind_selection)
        if (s.id == id)
            return true;
    return false;
}
void mindToggleSelect(UIState& ui, const std::string& id, UIState::MindSelectedItem::Location loc)
{
    // Clear opposite-location entries when a new-location click lands.
    bool had_opposite = false;
    for (const auto& s : ui.mind_selection)
        if (s.location != loc)
        {
            had_opposite = true;
            break;
        }
    if (had_opposite)
        ui.mind_selection.clear();
    auto it = std::find_if(ui.mind_selection.begin(), ui.mind_selection.end(),
                           [&](const UIState::MindSelectedItem& s) { return s.id == id; });
    if (it != ui.mind_selection.end())
        ui.mind_selection.erase(it);
    else
        ui.mind_selection.push_back({id, loc});
}
struct MindSelectionTally
{
    int observations = 0;
    int inferences = 0;
    int library = 0;
    int workbench = 0;
};
MindSelectionTally mindTally(const UIState& ui)
{
    MindSelectionTally t;
    for (const auto& s : ui.mind_selection)
    {
        if (s.location == UIState::MindSelectedItem::Location::Library)
            ++t.library;
        else
            ++t.workbench;
        if (selva::insight::kindOf(s.id) == selva::insight::NodeKind::Observation)
            ++t.observations;
        else
            ++t.inferences;
    }
    return t;
}

// Context menu (middle-click) on the workbench canvas. Populated
// dynamically based on current selection + hovered node target.
// Erase any selection entries whose id equals 'tgt'. Helper for the
// context-menu Deselect / Return-to-library handlers.
void mindEraseSelectionById(UIState& ui, const std::string& tgt)
{
    auto sit = std::remove_if(ui.mind_selection.begin(), ui.mind_selection.end(),
                              [&](const UIState::MindSelectedItem& s) { return s.id == tgt; });
    ui.mind_selection.erase(sit, ui.mind_selection.end());
}

// Workbench-context split: separates the workbench-selection set into
// observations + the first inference (if any).
struct MindWbSelection
{
    std::vector<std::string> obs;
    std::string inf;
};
MindWbSelection mindSplitWorkbenchSelection(const UIState& ui)
{
    MindWbSelection out;
    for (const auto& s : ui.mind_selection)
    {
        if (s.location != UIState::MindSelectedItem::Location::Workbench)
            continue;
        if (selva::insight::kindOf(s.id) == selva::insight::NodeKind::Observation)
            out.obs.push_back(s.id);
        else if (out.inf.empty())
            out.inf = s.id;
    }
    return out;
}

// Context-menu item: Infer. Active when 2+ observations are
// selected and no inferences.
void mindCtxItemInfer(UIState& ui, const std::vector<std::string>& sel_wb_obs,
                      const std::string& sel_wb_inf)
{
    const bool can_infer = sel_wb_obs.size() >= 2u && sel_wb_inf.empty();
    if (!ImGui::MenuItem("Infer", nullptr, false, can_infer))
        return;
    const std::string matched = selva::insight::matchInference(sel_wb_obs);
    ui.mind_feedback_success = !matched.empty();
    ui.mind_feedback_open_ticks_ms = SDL_GetTicks64();
    if (ui.mind_feedback_success)
    {
        ui.mind_picker_inference = matched;
        ui.mind_picker_evidence = sel_wb_obs;
        ui.mind_picker_existing_node.clear();
    }
}

// Context-menu item: Reconsider. Active when exactly 1 inference is
// selected (no observations).
void mindCtxItemReconsider(UIState& ui, const PlayerProfile& p,
                           const std::vector<std::string>& sel_wb_obs,
                           const std::string& sel_wb_inf, std::size_t tally_inferences)
{
    const bool can_reconsider =
        !sel_wb_inf.empty() && sel_wb_obs.empty() && tally_inferences == 1;
    if (!ImGui::MenuItem("Reconsider", nullptr, false, can_reconsider))
        return;
    for (const auto& w : p.workbench_inferences)
        if (w.id == sel_wb_inf)
        {
            ui.mind_picker_inference = w.id;
            ui.mind_picker_evidence = w.linked_observations;
            ui.mind_picker_existing_node = w.id;
            ImGui::OpenPopup("##reading-picker");
            break;
        }
}

// Context-menu item: Add evidence. Active when 1 inference + 1+
// observations are selected.
void mindCtxItemAddEvidence(UIState& ui, PlayerProfile& p,
                            const std::vector<std::string>& sel_wb_obs,
                            const std::string& sel_wb_inf)
{
    const bool can_add = !sel_wb_inf.empty() && !sel_wb_obs.empty();
    if (!ImGui::MenuItem("Add evidence", nullptr, false, can_add))
        return;
    for (auto& w : p.workbench_inferences)
    {
        if (w.id != sel_wb_inf)
            continue;
        for (const auto& sid : sel_wb_obs)
            if (std::find(w.linked_observations.begin(), w.linked_observations.end(), sid) ==
                w.linked_observations.end())
                w.linked_observations.push_back(sid);
        break;
    }
    std::vector<UIState::MindSelectedItem> kept;
    for (const auto& s : ui.mind_selection)
        if (s.id == sel_wb_inf)
            kept.push_back(s);
    ui.mind_selection = std::move(kept);
}

// Context-menu item: Return to library. Visible when target is a
// workbench node; locked observations refuse.
void mindCtxItemReturnToLibrary(UIState& ui, PlayerProfile& p, const std::string& tgt,
                                bool tgt_is_obs)
{
    const bool locked = tgt_is_obs && observationLockedByConclusion(p, tgt);
    if (!ImGui::MenuItem("Return to library", nullptr, false, !locked))
        return;
    if (tgt_is_obs)
    {
        auto it = std::remove_if(
            p.workbench_observations.begin(), p.workbench_observations.end(),
            [&](const PlayerProfile::WorkbenchNode& w) { return w.id == tgt; });
        p.workbench_observations.erase(it, p.workbench_observations.end());
    }
    else
    {
        auto it = std::remove_if(
            p.workbench_inferences.begin(), p.workbench_inferences.end(),
            [&](const PlayerProfile::WorkbenchNode& w) { return w.id == tgt; });
        p.workbench_inferences.erase(it, p.workbench_inferences.end());
    }
    mindEraseSelectionById(ui, tgt);
}

// Self-contained Begin/End pair; opened elsewhere via OpenPopup.
void renderMindContextMenu(UIState& ui, PlayerProfile& p)
{
    if (!ImGui::BeginPopup("##mind-context"))
        return;
    const std::string& tgt = ui.mind_context_target_id;
    const MindSelectionTally tally2 = mindTally(ui);
    const bool tgt_is_obs =
        !tgt.empty() && selva::insight::kindOf(tgt) == selva::insight::NodeKind::Observation;
    const bool tgt_is_inf =
        !tgt.empty() && selva::insight::kindOf(tgt) == selva::insight::NodeKind::Inference;
    const bool tgt_selected = !tgt.empty() && mindIsSelected(ui, tgt);

    if (tgt_selected)
    {
        if (ImGui::MenuItem("Deselect"))
            mindEraseSelectionById(ui, tgt);
        ImGui::Separator();
    }

    const MindWbSelection wb = mindSplitWorkbenchSelection(ui);
    mindCtxItemInfer(ui, wb.obs, wb.inf);
    mindCtxItemReconsider(ui, p, wb.obs, wb.inf, tally2.inferences);
    mindCtxItemAddEvidence(ui, p, wb.obs, wb.inf);

    if (tgt_is_obs || tgt_is_inf)
    {
        ImGui::Separator();
        mindCtxItemReturnToLibrary(ui, p, tgt, tgt_is_obs);
    }

    if (!ui.mind_carrying_id.empty())
    {
        ImGui::Separator();
        if (ImGui::MenuItem("Cancel carry"))
            ui.mind_carrying_id.clear();
    }

    ImGui::EndPopup();
}

// Hit-test pass: returns the id of the topmost node under the
// mouse + writes whether it was an inference via out param. Empty
// string when nothing is hovered.
std::string mindWorkbenchHitTest(const PlayerProfile& p, const ImVec2& wb_p0, const ImVec2& wb_avail,
                                 const ImVec2& mouse, bool& out_is_inf)
{
    constexpr float kObsR = 10.0f;
    constexpr float kConcR = 13.0f;
    out_is_inf = false;
    auto canvasPos = [&](float nx, float ny) -> ImVec2
    { return ImVec2(wb_p0.x + nx * wb_avail.x, wb_p0.y + ny * wb_avail.y); };
    std::string hovered_id;
    for (const auto& conc : p.workbench_inferences)
    {
        const ImVec2 cp = canvasPos(conc.x, conc.y);
        const float dx = mouse.x - cp.x, dy = mouse.y - cp.y;
        if (dx * dx + dy * dy <= kConcR * kConcR * 1.4f)
        {
            hovered_id = conc.id;
            out_is_inf = true;
        }
    }
    if (hovered_id.empty())
    {
        for (const auto& ob : p.workbench_observations)
        {
            const ImVec2 op = canvasPos(ob.x, ob.y);
            const float dx = mouse.x - op.x, dy = mouse.y - op.y;
            if (dx * dx + dy * dy <= kObsR * kObsR * 1.4f)
            {
                hovered_id = ob.id;
                out_is_inf = false;
            }
        }
    }
    return hovered_id;
}

// Handle the click-model on the workbench canvas: shift+click=select,
// carrying+click=place, click on node=pick up, click empty=clear.
void handleMindWorkbenchClick(UIState& ui, PlayerProfile& p, const std::string& hovered_id,
                              const ImVec2& wb_p0, const ImVec2& wb_avail, const ImVec2& mouse)
{
    const bool shift_held = ImGui::GetIO().KeyShift && ui.mind_carrying_id.empty();
    if (shift_held && !hovered_id.empty())
    {
        mindToggleSelect(ui, hovered_id, UIState::MindSelectedItem::Location::Workbench);
        return;
    }
    if (!ui.mind_carrying_id.empty())
    {
        const std::string cid = ui.mind_carrying_id;
        const auto k = selva::insight::kindOf(cid);
        const float nx = std::clamp((mouse.x - wb_p0.x) / wb_avail.x, 0.05f, 0.95f);
        const float ny = std::clamp((mouse.y - wb_p0.y) / wb_avail.y, 0.05f, 0.95f);
        if (ui.mind_carrying_origin == UIState::MindCarryOrigin::Library)
        {
            if (k == selva::insight::NodeKind::Observation && !isObservationOnWorkbench(p, cid))
                p.workbench_observations.push_back({cid, nx, ny});
        }
        else
        {
            for (auto& w : p.workbench_observations)
                if (w.id == cid)
                {
                    w.x = nx;
                    w.y = ny;
                }
            for (auto& w : p.workbench_inferences)
                if (w.id == cid)
                {
                    w.x = nx;
                    w.y = ny;
                }
        }
        ui.mind_carrying_id.clear();
        return;
    }
    if (!hovered_id.empty())
    {
        ui.mind_carrying_id = hovered_id;
        ui.mind_carrying_origin = UIState::MindCarryOrigin::Workbench;
        return;
    }
    ui.mind_selection.clear();
}

// Draw observation + inference nodes on the workbench canvas. Also
// renders selection halo + hover halo + carry-dim treatment per node.
void renderMindWorkbenchNodes(const UIState& ui, const PlayerProfile& p, const ImVec2& wb_p0,
                              const ImVec2& wb_avail, const std::string& hovered_id, ImDrawList* dl)
{
    constexpr float kObsR = 10.0f;
    constexpr float kConcR = 13.0f;
    auto canvasPos = [&](float nx, float ny) -> ImVec2
    { return ImVec2(wb_p0.x + nx * wb_avail.x, wb_p0.y + ny * wb_avail.y); };

    for (const auto& ob : p.workbench_observations)
    {
        const ImVec2 op = canvasPos(ob.x, ob.y);
        const bool sel = mindIsSelected(ui, ob.id);
        const bool carrying_this = ui.mind_carrying_id == ob.id;
        ImU32 fill = sel ? IM_COL32(140, 125, 100, 220) : IM_COL32(80, 75, 70, 200);
        ImU32 border = sel ? IM_COL32(220, 200, 160, 255) : IM_COL32(120, 110, 100, 220);
        if (carrying_this)
        {
            fill = IM_COL32(50, 48, 45, 150);
            border = IM_COL32(90, 85, 80, 200);
        }
        dl->AddCircleFilled(op, kObsR, fill);
        dl->AddCircle(op, kObsR, border, 0, sel ? 2.0f : 1.0f);
        if (hovered_id == ob.id)
            dl->AddCircle(op, kObsR + 3.0f, IM_COL32(230, 210, 170, 200), 0, 1.0f);
    }
    for (const auto& conc : p.workbench_inferences)
    {
        const ImVec2 cp = canvasPos(conc.x, conc.y);
        const bool warranted = !conc.linked_observations.empty();
        const bool carrying_this = ui.mind_carrying_id == conc.id;
        if (carrying_this)
        {
            dl->AddCircleFilled(cp, kConcR, IM_COL32(50, 48, 45, 150));
            dl->AddCircle(cp, kConcR, IM_COL32(90, 85, 80, 200), 0, 1.5f);
        }
        else if (warranted)
        {
            dl->AddCircleFilled(cp, kConcR, IM_COL32(190, 175, 150, 235));
            dl->AddCircle(cp, kConcR, IM_COL32(50, 45, 40, 255), 0, 2.0f);
        }
        else
        {
            dl->AddCircleFilled(cp, kConcR, IM_COL32(120, 110, 95, 180));
            dl->AddCircle(cp, kConcR, IM_COL32(150, 140, 120, 200), 0, 1.0f);
        }
        if (mindIsSelected(ui, conc.id))
            dl->AddCircle(cp, kConcR + 5.0f, IM_COL32(230, 210, 170, 230), 0, 2.0f);
        else if (hovered_id == conc.id)
            dl->AddCircle(cp, kConcR + 3.0f, IM_COL32(230, 210, 170, 200), 0, 1.0f);
    }
}

// Draw the requires/confirmer edges between workbench nodes (lines
// from each inference to its linked observations).
void renderMindWorkbenchEdges(const PlayerProfile& p, const ImVec2& wb_p0, const ImVec2& wb_avail,
                              ImDrawList* dl)
{
    auto canvasPos = [&](float nx, float ny) -> ImVec2
    { return ImVec2(wb_p0.x + nx * wb_avail.x, wb_p0.y + ny * wb_avail.y); };
    auto findObsPos = [&](const std::string& id) -> std::optional<ImVec2>
    {
        for (const auto& w : p.workbench_observations)
            if (w.id == id)
                return canvasPos(w.x, w.y);
        return std::nullopt;
    };
    auto drawDashed = [&](ImVec2 a, ImVec2 b, ImU32 col, float thick)
    {
        const float dx = b.x - a.x, dy = b.y - a.y;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len < 1.0f)
            return;
        const float seg = 6.0f, gap = 4.0f;
        for (float t = 0.0f; t < len; t += seg + gap)
        {
            const float t2 = std::min(t + seg, len);
            dl->AddLine(ImVec2(a.x + dx * t / len, a.y + dy * t / len),
                        ImVec2(a.x + dx * t2 / len, a.y + dy * t2 / len), col, thick);
        }
    };
    for (const auto& conc : p.workbench_inferences)
    {
        const ImVec2 cp = canvasPos(conc.x, conc.y);
        for (const auto& link_obs : conc.linked_observations)
        {
            auto op = findObsPos(link_obs);
            if (op.has_value())
                dl->AddLine(*op, cp, IM_COL32(140, 130, 115, 200), 1.5f);
        }
        for (const auto& link_conf : conc.linked_confirmers)
        {
            auto op = findObsPos(link_conf);
            if (op.has_value())
                drawDashed(*op, cp, IM_COL32(180, 160, 130, 220), 1.5f);
        }
    }
}

// Workbench canvas top-level: child window, edges, hit-test, click
// handler, middle-click context menu, node draw, carry preview.
// Reports the currently-hovered id via the out parameter.
void renderMindWorkbench(UIState& ui, PlayerProfile& p, float workbench_w, float panels_h,
                         std::string& mind_hover_id)
{
    ImGui::SameLine();
    ImGui::BeginChild("##mind-workbench", ImVec2(workbench_w, panels_h), true);
    const ImVec2 wb_p0 = ImGui::GetCursorScreenPos();
    const ImVec2 wb_avail = ImGui::GetContentRegionAvail();
    const ImVec2 wb_p1 = ImVec2(wb_p0.x + wb_avail.x, wb_p0.y + wb_avail.y);
    ImGui::InvisibleButton("##wb-canvas", wb_avail);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(wb_p0, wb_p1, true);
    dl->AddRectFilled(wb_p0, wb_p1, IM_COL32(28, 26, 24, 220), 4.0f);
    dl->AddRect(wb_p0, wb_p1, IM_COL32(70, 65, 60, 255), 4.0f);

    renderMindWorkbenchEdges(p, wb_p0, wb_avail, dl);

    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const bool canvas_hovered = ImGui::IsItemHovered();
    bool hovered_is_inf = false;
    const std::string hovered_id =
        mindWorkbenchHitTest(p, wb_p0, wb_avail, mouse, hovered_is_inf);
    (void)hovered_is_inf;

    if (canvas_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        handleMindWorkbenchClick(ui, p, hovered_id, wb_p0, wb_avail, mouse);

    if (canvas_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
    {
        ui.mind_context_target_id = hovered_id;
        ImGui::SetNextWindowPos(ImGui::GetIO().MousePos, ImGuiCond_Appearing, ImVec2(1.0f, 0.0f));
        ImGui::OpenPopup("##mind-context");
    }
    renderMindContextMenu(ui, p);

    renderMindWorkbenchNodes(ui, p, wb_p0, wb_avail, hovered_id, dl);

    dl->PopClipRect();

    if (!hovered_id.empty())
        mind_hover_id = hovered_id;

    if (p.workbench_observations.empty() && p.workbench_inferences.empty())
    {
        const char* hint =
            "Click an observation in the library to pick it up, then click here to place.";
        const ImVec2 ts = ImGui::CalcTextSize(hint);
        dl->AddText(
            ImVec2(wb_p0.x + (wb_avail.x - ts.x) * 0.5f, wb_p0.y + (wb_avail.y - ts.y) * 0.5f),
            IM_COL32(120, 110, 100, 200), hint);
    }
    ImGui::EndChild();

    // Carry preview: cursor-following circle on the foreground draw list.
    if (!ui.mind_carrying_id.empty())
    {
        const ImVec2 mp = ImGui::GetIO().MousePos;
        const auto k = selva::insight::kindOf(ui.mind_carrying_id);
        ImDrawList* fg = ImGui::GetForegroundDrawList();
        if (k == selva::insight::NodeKind::Inference)
        {
            fg->AddCircleFilled(mp, 13.0f, IM_COL32(190, 175, 150, 235));
            fg->AddCircle(mp, 13.0f, IM_COL32(50, 45, 40, 255), 0, 2.0f);
        }
        else
        {
            fg->AddCircleFilled(mp, 12.0f, IM_COL32(140, 125, 100, 230));
            fg->AddCircle(mp, 12.0f, IM_COL32(230, 210, 170, 255), 0, 2.0f);
        }
    }
}

// Info box -- full-width prose panel below the panels row. Reads the
// currently-attended node (hover wins, else exactly-one selection,
// else carry). The page's only read-surface.
void renderMindInfoBox(const UIState& ui, const PlayerProfile& p, const std::string& mind_hover_id,
                       float info_box_h)
{
    ImGui::Spacing();
    std::string info_id = mind_hover_id;
    if (info_id.empty() && ui.mind_selection.size() == 1)
        info_id = ui.mind_selection.front().id;
    if (info_id.empty() && !ui.mind_carrying_id.empty())
        info_id = ui.mind_carrying_id;

    ImGui::BeginChild("##mind-infobox", ImVec2(0.0f, info_box_h), true,
                      ImGuiWindowFlags_NoScrollbar);
    if (info_id.empty())
    {
        ImGui::TextDisabled("Hover a node to read its meaning.");
        ImGui::EndChild();
        return;
    }
    const auto k = selva::insight::kindOf(info_id);
    std::string kind_label = "Observation";
    std::string body;
    if (k == selva::insight::NodeKind::Inference)
    {
        bool warranted = false;
        for (const auto& w : p.workbench_inferences)
        {
            if (w.id != info_id)
                continue;
            warranted = !w.linked_observations.empty();
            if (!w.reading_id.empty())
            {
                const auto rds = selva::insight::readingsOf(w.id);
                for (const auto& r : rds)
                    if (r.id == w.reading_id)
                    {
                        body = r.text;
                        break;
                    }
            }
            break;
        }
        kind_label = warranted ? "Inference [warranted]" : "Inference [unwarranted]";
    }
    if (body.empty())
        body = selva::lang::resolve("mind." + info_id + ".summary");
    ImGui::TextDisabled("%s", kind_label.c_str());
    ImGui::Separator();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextWrapped("%s", body.c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndChild();
}

// Library panel -- left column of the Mind sub-page. Renders a grid
// of library observations as circles + the "return to library"
// click-target for carried nodes. Reports the currently-hovered id
// via the out parameter.
// Pick the fill color for a library node based on its state.
ImU32 mindLibNodeFill(bool carrying_this, bool selected, bool hovered)
{
    if (carrying_this)
        return IM_COL32(50, 48, 45, 150);
    if (selected)
        return IM_COL32(140, 125, 100, 230);
    if (hovered)
        return IM_COL32(120, 110, 95, 220);
    return IM_COL32(80, 75, 70, 200);
}
ImU32 mindLibNodeBorder(bool carrying_this, bool selected, bool hovered)
{
    if (carrying_this)
        return IM_COL32(90, 85, 80, 200);
    if (selected)
        return IM_COL32(230, 210, 170, 255);
    if (hovered)
        return IM_COL32(220, 200, 160, 255);
    return IM_COL32(120, 110, 100, 220);
}

// Render a single library observation circle. Updates ui.mind_carrying_id
// on click and writes mind_hover_id on hover.
void renderMindLibNode(UIState& ui, const std::string& id, const ImVec2& c, float r,
                       ImDrawList* dlx, std::string& mind_hover_id)
{
    ImGui::SetCursorScreenPos(ImVec2(c.x - r, c.y - r));
    ImGui::PushID(id.c_str());
    ImGui::InvisibleButton("##libnode", ImVec2(r * 2.0f, r * 2.0f));
    const bool hovered = ImGui::IsItemHovered();
    const bool selected = mindIsSelected(ui, id);
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        ui.mind_carrying_id = id;
        ui.mind_carrying_origin = UIState::MindCarryOrigin::Library;
    }
    const bool carrying_this = ui.mind_carrying_id == id;
    dlx->AddCircleFilled(c, r, mindLibNodeFill(carrying_this, selected, hovered));
    dlx->AddCircle(c, r, mindLibNodeBorder(carrying_this, selected, hovered), 0,
                   selected ? 2.0f : (hovered ? 1.5f : 1.0f));
    if (hovered)
        mind_hover_id = id;
    ImGui::PopID();
}

// Return-to-library handler invoked when the player clicks the
// library-area-as-drop-target while carrying. Respects observation lock.
void mindHandleReturnCarryToLibrary(UIState& ui, PlayerProfile& p)
{
    const std::string drop_id = ui.mind_carrying_id;
    const auto k = selva::insight::kindOf(drop_id);
    bool removed = false;
    if (k == selva::insight::NodeKind::Observation)
    {
        if (!observationLockedByConclusion(p, drop_id))
        {
            auto it = std::remove_if(
                p.workbench_observations.begin(), p.workbench_observations.end(),
                [&](const PlayerProfile::WorkbenchNode& w) { return w.id == drop_id; });
            if (it != p.workbench_observations.end())
            {
                p.workbench_observations.erase(it, p.workbench_observations.end());
                removed = true;
            }
        }
    }
    else
    {
        auto it = std::remove_if(
            p.workbench_inferences.begin(), p.workbench_inferences.end(),
            [&](const PlayerProfile::WorkbenchNode& w) { return w.id == drop_id; });
        if (it != p.workbench_inferences.end())
        {
            p.workbench_inferences.erase(it, p.workbench_inferences.end());
            removed = true;
        }
    }
    if (removed)
        mindEraseSelectionById(ui, drop_id);
    ui.mind_carrying_id.clear();
}

void renderMindLibrary(UIState& ui, PlayerProfile& p,
                       const std::vector<std::string>& library_observations, float library_w,
                       float panels_h, std::string& mind_hover_id)
{
    ImGui::BeginChild("##mind-library", ImVec2(library_w, panels_h), true);
    ImGui::TextDisabled("Observations");
    ImGui::Separator();
    ImGui::Spacing();
    constexpr float kLibNodeR = 12.0f;
    constexpr float kLibNodePad = 8.0f;
    const float row_w = ImGui::GetContentRegionAvail().x;
    const float cell = kLibNodeR * 2.0f + kLibNodePad;
    const int per_row = std::max(1, static_cast<int>(row_w / cell));
    const float origin_x = ImGui::GetCursorScreenPos().x;
    const float origin_y = ImGui::GetCursorScreenPos().y;
    ImDrawList* dlx = ImGui::GetWindowDrawList();
    for (std::size_t i = 0; i < library_observations.size(); ++i)
    {
        const std::string& id = library_observations[i];
        const int row = static_cast<int>(i) / per_row;
        const int col = static_cast<int>(i) % per_row;
        const ImVec2 c(origin_x + col * cell + kLibNodeR + kLibNodePad * 0.5f,
                       origin_y + row * cell + kLibNodeR + kLibNodePad * 0.5f);
        renderMindLibNode(ui, id, c, kLibNodeR, dlx, mind_hover_id);
    }
    const int rows = (static_cast<int>(library_observations.size()) + per_row - 1) / per_row;
    ImGui::SetCursorScreenPos(ImVec2(origin_x, origin_y + rows * cell));
    const ImVec2 dz_avail = ImGui::GetContentRegionAvail();
    if (dz_avail.x > 1.0f && dz_avail.y > 1.0f)
    {
        ImGui::InvisibleButton("##mind-lib-clicktarget", dz_avail);
        if (!ui.mind_carrying_id.empty() && ImGui::IsItemHovered() &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            mindHandleReturnCarryToLibrary(ui, p);
    }
    ImGui::EndChild();
}

// Reading picker modal -- opens via the Infer feedback flow OR via
// Reconsider on a workbench inference. Self-contained Begin/End pair;
// the modal is opened elsewhere via OpenPopup("##reading-picker").
// Commit a newly-deduced inference: fires the insight via commitDeduce,
// computes a centroid-based canvas position from the player's selected
// evidence, and appends a workbench node.
void mindCommitNewInference(PlayerProfile& p, const UIState& ui, const std::string& inference_id,
                            const std::string& stored_reading_id)
{
    const std::string fired_id = selva::insight::commitDeduce(inference_id);
    if (fired_id.empty() || isConclusionOnWorkbench(p, fired_id))
        return;
    float sx = 0.0f, sy = 0.0f;
    int cnt = 0;
    for (const auto& sid : ui.mind_picker_evidence)
        for (const auto& w : p.workbench_observations)
            if (w.id == sid)
            {
                sx += w.x;
                sy += w.y;
                ++cnt;
            }
    const float cx = cnt > 0 ? sx / cnt : 0.5f;
    const float cy = cnt > 0 ? std::max(0.10f, sy / cnt - 0.10f) : 0.5f;
    PlayerProfile::WorkbenchNode w;
    w.id = fired_id;
    w.x = cx;
    w.y = cy;
    w.linked_observations = ui.mind_picker_evidence;
    w.reading_id = stored_reading_id;
    p.workbench_inferences.push_back(std::move(w));
}

// Apply the player's reading choice to either an existing workbench
// inference (Reconsider) or a freshly-deduced one (Infer). Clears
// picker state + bumps Intelligence + closes the modal.
void mindApplyReadingChoice(UIState& ui, PlayerProfile& p, const std::string& chosen_reading_id)
{
    const std::string stored_reading_id =
        (chosen_reading_id == "_empty_") ? std::string{} : chosen_reading_id;
    const std::string inference_id = ui.mind_picker_inference;
    const bool is_reconsider = !ui.mind_picker_existing_node.empty();
    if (is_reconsider)
    {
        for (auto& w : p.workbench_inferences)
            if (w.id == ui.mind_picker_existing_node)
            {
                w.reading_id = stored_reading_id;
                break;
            }
    }
    else
    {
        mindCommitNewInference(p, ui, inference_id, stored_reading_id);
    }
    if (!ui.mind_picker_evidence.empty())
        selva::growIntelligence();
    ui.mind_selection.clear();
    ui.mind_picker_inference.clear();
    ui.mind_picker_evidence.clear();
    ui.mind_picker_existing_node.clear();
    ImGui::CloseCurrentPopup();
}

void renderMindReadingPicker(UIState& ui, PlayerProfile& p)
{
    if (!ImGui::BeginPopupModal("##reading-picker", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize))
        return;
    const auto readings = selva::insight::readingsOf(ui.mind_picker_inference);
    ImGui::TextDisabled("Pick a reading:");
    ImGui::Separator();
    std::string chosen_reading_id;
    if (readings.empty())
    {
        ImGui::TextDisabled("(no readings authored for this inference yet)");
        if (ImGui::Button("Commit without reading"))
            chosen_reading_id = "_empty_";
        ImGui::SameLine();
    }
    for (const auto& rd : readings)
    {
        ImGui::PushID(rd.id.c_str());
        if (ImGui::Selectable(rd.text.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick,
                              ImVec2(560.0f, 0.0f)))
            chosen_reading_id = rd.id;
        ImGui::PopID();
        ImGui::Spacing();
    }
    ImGui::Separator();
    if (ImGui::Button("Cancel"))
    {
        ui.mind_picker_inference.clear();
        ui.mind_picker_evidence.clear();
        ui.mind_picker_existing_node.clear();
        ImGui::CloseCurrentPopup();
    }
    if (!chosen_reading_id.empty())
        mindApplyReadingChoice(ui, p, chosen_reading_id);
    ImGui::EndPopup();
}

// Two-stage feedback overlay (post-Infer). Shows "you can infer" or
// "you cannot infer" centered on screen for a dwell, then either
// transitions to the reading picker (on success) or auto-closes (on
// failure).
void renderMindFeedbackOverlay(UIState& ui)
{
    if (ui.mind_feedback_open_ticks_ms == 0)
        return;
    constexpr std::uint64_t kFeedbackDwellMs = 1500;
    const std::uint64_t now = SDL_GetTicks64();
    const std::uint64_t elapsed = now - ui.mind_feedback_open_ticks_ms;
    const ImVec2 disp = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(disp.x * 0.5f, disp.y * 0.5f), ImGuiCond_Always,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowBgAlpha(0.90f);
    if (ImGui::Begin("##mind-feedback", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize |
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs))
    {
        ImGui::PushTextWrapPos(420.0f);
        ImGui::TextWrapped("%s", ui.mind_feedback_success
                                     ? "Something connects. An inference is forming."
                                     : "Nothing connects. The observations do not yet form one.");
        ImGui::PopTextWrapPos();
    }
    ImGui::End();
    if (elapsed >= kFeedbackDwellMs)
    {
        const bool was_success = ui.mind_feedback_success;
        ui.mind_feedback_open_ticks_ms = 0;
        ui.mind_feedback_success = false;
        if (was_success)
            ImGui::OpenPopup("##reading-picker");
        else
            ui.mind_picker_inference.clear();
    }
}

} // namespace

void renderPauseVesselMind()
{
    ImGui::Spacing();
    // Cognitive stat strip -- visible feedback for the player as they
    // engage with the workbench. Mind page is where these stats are
    // EARNED so they belong here too (Form page also shows them).
    {
        const auto& act = selva::gameplay::player();
        ImGui::TextDisabled("PER %d   COG %d   INT %d", act.stats.per, act.stats.cog,
                            act.stats.intl);
        ImGui::Separator();
        ImGui::Spacing();
    }
    PlayerProfile* pp = selva::activePlayerProfile();
    if (pp == nullptr || pp->unlocked_insights.empty())
    {
        ImGui::TextDisabled("(nothing yet)");
        return;
    }
    PlayerProfile& p = *pp;
    auto& ui = uiState();

    // Split: library observations are unlocked observations NOT on
    // the workbench. Conclusions on the workbench render on canvas;
    // Library is OBSERVATIONS ONLY. Conclusions never appear in the
    // library; if removed from the workbench, they cease to be
    // visible (and can be re-deduced from their requires). Flat list,
    // no grouping. Per the locked design 2026-06-09 v2.
    const std::vector<std::string> fired = selva::insight::firedInsights();
    std::vector<std::string> library_observations;
    for (const auto& id : fired)
    {
        if (selva::insight::kindOf(id) != selva::insight::NodeKind::Observation)
            continue;
        if (!isObservationOnWorkbench(p, id))
            library_observations.push_back(id);
    }

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float library_w = std::max(220.0f, avail.x * 0.25f);
    const float workbench_w = avail.x - library_w - 8.0f;
    // Reserve: 140px info box (full-width below the panels), 28px
    // status strip, 8px spacing -> ~176px total below the panels row.
    constexpr float kInfoBoxH = 140.0f;
    constexpr float kStatusStripH = 28.0f;
    const float panels_h = std::max(220.0f, avail.y - kInfoBoxH - kStatusStripH - 16.0f);

    // The node id currently under the mouse on EITHER surface (library
    // or workbench). Populated by the panels below; consumed by the
    // info box at the bottom of the page to render hovered-node prose.
    std::string mind_hover_id;

    renderMindLibrary(ui, p, library_observations, library_w, panels_h, mind_hover_id);

    renderMindWorkbench(ui, p, workbench_w, panels_h, mind_hover_id);
    renderMindInfoBox(ui, p, mind_hover_id, kInfoBoxH);

    // ---- STATUS STRIP ----
    // Only transient state lives here now: carrying / selection count.
    // Gesture-teaching strings (Click to move, Shift+click to select,
    // etc.) belong in the future global help mode -- not on this
    // page, where they pollute the read.
    if (!ui.mind_carrying_id.empty())
        ImGui::TextDisabled("Carrying: %s", libraryShortLabel(ui.mind_carrying_id).c_str());
    else if (!ui.mind_selection.empty())
        ImGui::TextDisabled("%zu selected", ui.mind_selection.size());

    renderMindFeedbackOverlay(ui);
    renderMindReadingPicker(ui, p);
}

// Top-level Vessel tab. Draws the sub-page selector + dispatches to
// the active sub-page. Sub-page selection persists in UIState across
// pause-menu opens.
void renderPauseVesselTab()
{
    auto& ui = uiState();
    // Sub-page selector: simple horizontal button row. ImGui Selectable
    // with SameLine() keeps the buttons compact + lets a future pass
    // restyle without changing the dispatch logic.
    auto subpage_button = [&](const char* label, UIState::VesselSubpage page)
    {
        const bool active = (ui.vessel_subpage == page);
        if (active)
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::Button(label))
            ui.vessel_subpage = page;
        if (active)
            ImGui::PopStyleColor();
    };
    subpage_button("Overview", UIState::VesselSubpage::Overview);
    ImGui::SameLine();
    subpage_button("Form", UIState::VesselSubpage::Form);
    ImGui::SameLine();
    subpage_button("Hands", UIState::VesselSubpage::Hands);
    ImGui::SameLine();
    subpage_button("Mind", UIState::VesselSubpage::Mind);
    ImGui::Separator();
    switch (ui.vessel_subpage)
    {
    case UIState::VesselSubpage::Overview:
        renderPauseVesselOverview();
        break;
    case UIState::VesselSubpage::Form:
        renderPauseVesselForm();
        break;
    case UIState::VesselSubpage::Hands:
        renderPauseVesselHands();
        break;
    case UIState::VesselSubpage::Mind:
        renderPauseVesselMind();
        break;
    }
}

namespace
{
struct InventoryEntryLabel
{
    std::string item_id;
    std::string display;
};

InventoryEntryLabel buildInventoryEntryLabel(const selva::items::Entry& e,
                                             const selva::items::ItemRegistry& items)
{
    InventoryEntryLabel out;
    std::string suffix;
    if (const auto* p = std::get_if<selva::items::PossessionEntry>(&e))
        out.item_id = p->item_id;
    else if (const auto* s = std::get_if<selva::items::StackEntry>(&e))
    {
        out.item_id = s->item_id;
        suffix = "  x" + std::to_string(s->count);
    }
    else if (const auto* x = std::get_if<selva::items::InstancedEntry>(&e))
    {
        out.item_id = x->item_id;
        if (x->upgrade_level > 0)
            suffix = "  +" + std::to_string(x->upgrade_level);
    }
    const selva::items::ItemDef* def = items.get(out.item_id);
    out.display = (def != nullptr ? def->display_name : out.item_id) + suffix;
    return out;
}

void drawInventoryCategoryTabs(const std::vector<selva::items::CategoryDef>& cats,
                               std::string& selected_category, std::string& selected_item)
{
    if (!ImGui::BeginTabBar("##inv_cats"))
        return;
    for (const auto& cat : cats)
    {
        if (!ImGui::BeginTabItem(cat.display_name.c_str()))
            continue;
        if (selected_category != cat.id)
        {
            selected_category = cat.id;
            selected_item.clear();
        }
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
}

void drawInventoryEntryList(const std::vector<selva::items::Entry>* entries,
                            const selva::items::ItemRegistry& items, std::string& selected_item)
{
    if (entries == nullptr || entries->empty())
    {
        ImGui::TextDisabled("(empty)");
        return;
    }
    for (const auto& e : *entries)
    {
        const InventoryEntryLabel label = buildInventoryEntryLabel(e, items);
        const bool selected = (selected_item == label.item_id);
        if (ImGui::Selectable(label.display.c_str(), selected))
            selected_item = label.item_id;
    }
}

void drawInventoryUseButton(const selva::items::ItemDef& sel, selva::items::Inventory& inv,
                            std::string& selected_item)
{
    if (sel.use_handler.empty())
        return;
    selva::items::UseGate gate;
    if (!sel.use_condition.empty())
    {
        const auto* cond = selva::items::getUseCondition(sel.use_condition);
        if (cond != nullptr)
            gate = (*cond)();
    }
    if (!gate.enabled)
        ImGui::BeginDisabled();
    if (ImGui::Button("Use"))
    {
        const auto* action = selva::items::getUseAction(sel.use_handler);
        if (action != nullptr)
            (*action)(inv, selected_item);
        selected_item.clear();
    }
    if (!gate.enabled)
    {
        ImGui::EndDisabled();
        if (!gate.disabled_reason.empty() && ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", gate.disabled_reason.c_str());
    }
}

void drawInventoryDetailPanel(const std::string& selected_item, selva::items::Inventory& inv,
                              const selva::items::ItemRegistry& items,
                              std::string& selected_item_ref)
{
    const selva::items::ItemDef* sel = items.get(selected_item);
    if (sel == nullptr)
    {
        ImGui::TextDisabled("Select an item.");
        return;
    }
    ImGui::TextUnformatted(sel->display_name.c_str());
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextWrapped("%s", sel->description.c_str());
    ImGui::Spacing();
    drawInventoryUseButton(*sel, inv, selected_item_ref);
}
} // namespace

void renderPauseInventoryTab()
{
    ImGui::Spacing();
    PlayerProfile* profile = activePlayerProfile();
    if (profile == nullptr)
    {
        ImGui::TextDisabled("(no active character)");
        return;
    }
    selva::items::Inventory& inv = profile->inventory;
    const auto& cats = selva::items::categoryRegistry().all();
    const auto& items = selva::items::itemRegistry();
    if (cats.empty())
    {
        ImGui::TextDisabled("(no inventory categories loaded)");
        return;
    }
    static std::string sSelectedCategory;
    static std::string sSelectedItem;
    if (sSelectedCategory.empty())
        sSelectedCategory = cats.front().id;
    drawInventoryCategoryTabs(cats, sSelectedCategory, sSelectedItem);
    const std::vector<selva::items::Entry>* entries =
        selva::items::entriesIn(inv, sSelectedCategory);
    ImGui::Spacing();
    constexpr float kLeftPaneWidth = 240.0f;
    ImGui::BeginChild("##inv_list", ImVec2(kLeftPaneWidth, 240), true);
    drawInventoryEntryList(entries, items, sSelectedItem);
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##inv_detail", ImVec2(0, 240), true);
    drawInventoryDetailPanel(sSelectedItem, inv, items, sSelectedItem);
    ImGui::EndChild();
}

} // namespace

bool renderPauseMenu()
{
    bool quit = false;
    // Full-screen darkening scrim behind the pause panel so the game
    // dims to background. The pause panel itself is sized at 85% of
    // the viewport, leaving a margin on all sides. Souls-style scale
    // -- the menu dominates the screen but doesn't touch the edges.
    selva::ui::drawFullScreenBackdrop(0.60f);
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const ImVec2 panel_size(vp->Size.x * 0.85f, vp->Size.y * 0.85f);
    beginCenteredWindow("##pause", panel_size);
    ImGui::SetWindowFontScale(1.6f);
    ImGui::TextUnformatted("Paused");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    auto& ui = uiState();
    if (ImGui::BeginTabBar("##pause_tabs"))
    {
        if (ImGui::BeginTabItem("Vessel"))
        {
            ui.menu_tab = UIState::Tab::Vessel;
            renderPauseVesselTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Inventory"))
        {
            ui.menu_tab = UIState::Tab::Inventory;
            renderPauseInventoryTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("System"))
        {
            ui.menu_tab = UIState::Tab::System;
            quit = renderSystemTab() || quit;
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::End();
    return quit;
}

// ---------------------------------------------------------------------------
// Pause-menu open/close trigger. ESC during Playing opens the pause
// overlay. ESC or RMB closes it (RMB doubles as the back gesture inside
// any menu). Suppressed for one frame after the overlay closes to keep
// the game from immediately re-pausing.
//
// Note: RMB does NOT open the pause menu - during Playing the cursor is
// captured for mouse-look and RMB is the combat block input. RMB only
// closes the pause menu (where the cursor is visible).
// ---------------------------------------------------------------------------
void tickPauseToggle()
{
    auto& ui = uiState();
    if (ui.input_suppressed)
    {
        ui.input_suppressed = false;
        return;
    }
    const bool esc = ImGui::IsKeyPressed(ImGuiKey_Escape);
    const bool rmb = rmbClicked();
    if (ui.active_screen == UIState::Screen::None)
    {
        if (esc)
        {
            ui.active_screen = UIState::Screen::Menu;
            // Elden-Ring-style autosave: persist on pause-menu open so
            // a crash or Quit-to-Desktop from the pause menu doesn't
            // lose progress. Quit-to-Main-Menu also calls flushAndSave
            // explicitly; the double-save is harmless and idempotent.
            if (gameState().phase == GameState::Phase::Playing)
                flushAndSave();
        }
    }
    else if (esc && !ui.mind_carrying_id.empty())
    {
        // Esc while carrying a Mind node cancels the carry and stays
        // in the pause menu, rather than closing the menu.
        ui.mind_carrying_id.clear();
        ui.input_suppressed = true;
    }
    else if (esc || rmb)
    {
        ui.active_screen = UIState::Screen::None;
        ui.input_suppressed = true;
        // Reset Mind canvas transient state. Persistent workbench
        // positions live on PlayerProfile and are NOT cleared here.
        ui.mind_selection.clear();
        ui.mind_carrying_id.clear();
        ui.mind_context_target_id.clear();
        ui.mind_picker_inference.clear();
        ui.mind_picker_evidence.clear();
        ui.mind_picker_existing_node.clear();
        ui.mind_feedback_open_ticks_ms = 0;
        ui.mind_feedback_success = false;
    }
}

// ---------------------------------------------------------------------------
// Mouse-capture toggle. When transitioning between menu (cursor visible) and
// playing (cursor captured for mouse-look), this is the single place that
// touches SDL_SetRelativeMouseMode. The screens flip sJustEnteredPlaying /
// sJustLeftPlaying on Phase transitions; this function consumes and clears
// those flags.
// ---------------------------------------------------------------------------
void tickMouseCapture()
{
    if (sJustEnteredPlaying)
    {
        SDL_SetRelativeMouseMode(SDL_TRUE);
        SDL_GetRelativeMouseState(nullptr, nullptr);
        sJustEnteredPlaying = false;
    }
    if (sJustLeftPlaying)
    {
        SDL_SetRelativeMouseMode(SDL_FALSE);
        sJustLeftPlaying = false;
    }

    // Also: if we're in pause overlay or F1 tuning panel, release the
    // mouse; if we close them, re-capture. Without including the F1
    // panel here, the F1-toggle's SDL_SetRelativeMouseMode call gets
    // overwritten on the next frame by this routine.
    auto& gs = gameState();
    auto& ui = uiState();
    if (gs.phase == GameState::Phase::Playing)
    {
        const bool tuning_open = selva::gameplay::tickstate::showTuningPanel();
        const bool dialog_open = selva::text::active();
        const bool picker_open = selva::ui::classPickerActive();
        const bool name_prompt_open = selva::ui::namePromptActive();
        const bool want_relative =
            !ui.isScreenOpen() && !tuning_open && !dialog_open && !picker_open && !name_prompt_open;
        const bool is_relative = (SDL_GetRelativeMouseMode() == SDL_TRUE);
        if (want_relative != is_relative)
        {
            SDL_SetRelativeMouseMode(want_relative ? SDL_TRUE : SDL_FALSE);
            SDL_ShowCursor(want_relative ? SDL_DISABLE : SDL_ENABLE);
            if (want_relative)
                SDL_GetRelativeMouseState(nullptr, nullptr);
        }
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Public entry point - dispatch on Phase + UIState. Called once per frame
// from selvaRenderImGui (which is registered with engine.setRenderImGui in
// main.cpp).
// ---------------------------------------------------------------------------
bool renderScreens(Engine& /*engine*/)
{
    bool quit = false;
    auto& gs = gameState();

    switch (gs.phase)
    {
    case GameState::Phase::MainMenu:
        quit = renderMainMenu();
        break;
    case GameState::Phase::LoadGame:
        renderLoadGame();
        break;
    case GameState::Phase::Settings:
        renderSettings();
        break;
    case GameState::Phase::Playing:
        tickPauseToggle();
        if (uiState().isScreenOpen())
            quit = renderPauseMenu() || quit;
        break;
    }

    tickMouseCapture();
    return quit;
}

} // namespace selva::ui
