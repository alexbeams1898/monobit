#include "ui/Screens.h"

#include "AppState.h"
#include "AppStateGlobal.h"
#include "Engine.h"
#include "SaveManager.h"
#include "ecs/GameComponents.h"
#include "ecs/ItemConfig.h"
#include "gameplay/PerFrameTick.h"
#include "ops/InventoryOps.h"

#include <imgui.h>

#include <SDL.h>

#include <algorithm>
#include <cstring>
#include <string>

// ---------------------------------------------------------------------------
// Screens.cpp - ImGui-driven screen rendering for Selva's main menu, character
// create, load game, settings, and pause overlay.
//
// All screens share the same TU because they're small, ImGui-based, and
// share state through the AppStateGlobal singletons. The single entry point
// renderScreens() dispatches on GameState::phase + UIState. Each screen
// updates GameState/SaveData directly when the user takes an action.
//
// Modeled on prison-escape-game's screens/* family (MainMenuScreen,
// CharCreateScreen, LoadGameScreen, SettingsScreen, PauseMenu). The
// per-screen behavior is parallel; only the ImGui rendering is Selva-style
// instead of UIRenderer-style.
// ---------------------------------------------------------------------------

namespace selva::ui
{

namespace
{

// Six-letter name buffer for character creation (per story.md - the Guide
// elicits six letters for the Vagrant's name). The buffer holds 7 to leave
// room for the null terminator.
constexpr int kNameMaxLen = 6;
char sNameBuf[kNameMaxLen + 1] = "";

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
        sJustLeftPlaying = true;
    if (!was_playing && will_be_playing)
        sJustEnteredPlaying = true;
    gs.phase = next;
}

// Center the next window's contents in the screen. ImGui-idiomatic helper.
void beginCenteredWindow(const char* title, ImVec2 size)
{
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const ImVec2 pos(vp->Pos.x + (vp->Size.x - size.x) * 0.5f,
                     vp->Pos.y + (vp->Size.y - size.y) * 0.5f);
    ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    ImGui::Begin(title, nullptr,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoTitleBar);
}

// Centered button helper - draws a button of fixed width in the current
// window's content region.
bool centeredButton(const char* label, float width = 200.0f)
{
    const float region = ImGui::GetContentRegionAvail().x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (region - width) * 0.5f);
    return ImGui::Button(label, ImVec2(width, 0));
}

// True on the frame the right mouse button is pressed (transitions from
// up to down). ImGui forwards SDL mouse events so this works in menu
// contexts; in Playing the cursor is captured for mouse-look so RMB
// would not fire here.
bool rmbClicked()
{
    return ImGui::IsMouseClicked(ImGuiMouseButton_Right);
}

// Back/cancel input - ESC or RMB. Matches prison-escape's pattern; see
// games/prison-escape-game/src/screens/* for the canonical examples.
// Caller is responsible for consuming this in a single place per frame.
bool wantBack()
{
    return ImGui::IsKeyPressed(ImGuiKey_Escape) || rmbClicked();
}

// Helper-controls hint bar - a borderless, transparent window placed just
// below the modal it accompanies. Anchored to (cx, modal_bottom + gap) where
// cx is the modal's horizontal center. Used to teach players the ESC/RMB-
// back binding without claiming a button slot inside the modal.
//
// Call AFTER the modal's ImGui::End() so the modal's window rect is set on
// the previous frame for layout (we use a stable position derived from the
// viewport, not the previous window's actual rect, since ImGui doesn't
// expose that cleanly mid-frame).
void drawHintBar(const char* text, ImVec2 modal_size)
{
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    constexpr float kGap = 8.0f;
    const float modal_top = vp->Pos.y + (vp->Size.y - modal_size.y) * 0.5f;
    const float modal_bottom = modal_top + modal_size.y;

    const float w = ImGui::CalcTextSize(text).x + 24.0f;
    const float h = ImGui::GetFontSize() + 16.0f;
    const ImVec2 pos(vp->Pos.x + (vp->Size.x - w) * 0.5f, modal_bottom + kGap);
    const ImVec2 size(w, h);

    ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::Begin("##hint", nullptr,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoInputs |
                     ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);
    ImGui::TextDisabled("%s", text);
    ImGui::End();
}

// Flush the runtime player's persistent state into the active character's
// PlayerProfile, then write the SaveData to disk. Called by every save
// entry point - pause-menu Save button, Quit-to-Main-Menu - so what's on
// disk reflects what's in the running game. If the active character isn't
// found in the save (defensive; shouldn't happen via normal flow), just
// writes the save with no flush.
void flushAndSave()
{
    auto& sd = saveData();
    const std::string& active = gameState().active_character;
    for (auto& c : sd.characters)
    {
        if (c.name == active)
        {
            selva::gameplay::saveActiveCharacterFromPlayer(c);
            break;
        }
    }
    SaveManager::save(sd);
    uiState().last_save_ticks_ms = SDL_GetTicks64();
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
        std::memset(sNameBuf, 0, sizeof(sNameBuf));
        setPhase(GameState::Phase::CharCreate);
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
// Character create screen
// ---------------------------------------------------------------------------
void renderCharCreate()
{
    beginCenteredWindow("##charcreate", ImVec2(360, 260));
    ImGui::SetWindowFontScale(1.3f);
    ImGui::TextUnformatted("Enter thy name");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::Spacing();
    ImGui::TextDisabled("(six letters)");
    ImGui::Spacing();

    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputText("##name", sNameBuf, sizeof(sNameBuf),
                     ImGuiInputTextFlags_CharsUppercase | ImGuiInputTextFlags_CharsNoBlank);

    ImGui::Spacing();
    ImGui::Spacing();

    const int name_len = static_cast<int>(std::strlen(sNameBuf));
    const bool name_valid = name_len > 0;
    const bool name_taken = [&]()
    {
        for (const auto& c : saveData().characters)
            if (c.name == sNameBuf)
                return true;
        return false;
    }();

    if (name_taken)
        ImGui::TextColored(ImVec4(1, 0.5f, 0.5f, 1), "Name already exists.");
    else
        ImGui::NewLine();

    if (!name_valid || name_taken)
        ImGui::BeginDisabled();
    if (centeredButton("Begin", 160.0f))
    {
        SaveManager::addCharacter(saveData(), sNameBuf);
        SaveManager::save(saveData());
        gameState().active_character = sNameBuf;
        gameState().pending_world_create = true;
        setPhase(GameState::Phase::Playing);
    }
    if (!name_valid || name_taken)
        ImGui::EndDisabled();

    ImGui::End();
    drawHintBar("[Esc/RMB] Back", ImVec2(360, 260));

    if (wantBack())
        setPhase(GameState::Phase::MainMenu);
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
        if (ImGui::Button(c.name.c_str(), ImVec2(200, 0)))
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
bool renderPauseMenu()
{
    bool quit = false;
    beginCenteredWindow("##pause", ImVec2(460, 460));
    ImGui::SetWindowFontScale(1.3f);
    ImGui::TextUnformatted("Paused");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    auto& ui = uiState();
    if (ImGui::BeginTabBar("##pause_tabs"))
    {
        if (ImGui::BeginTabItem("Status"))
        {
            ui.menu_tab = UIState::Tab::Status;
            ImGui::Spacing();
            ImGui::Text("Character: %s", gameState().active_character.c_str());
            ImGui::Spacing();
            ImGui::TextDisabled("(stats, sangue totals, evolution stage TBD)");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Inventory"))
        {
            ui.menu_tab = UIState::Tab::Inventory;
            ImGui::Spacing();
            const auto& inv = playerInventory();
            ImGui::Text("Slots: %d / %d", static_cast<int>(inv.items.size()), inv.max_slots);
            ImGui::Spacing();
            if (inv.items.empty())
                ImGui::TextDisabled("(empty - no items yet)");
            else
            {
                const auto& reg = itemRegistry();
                for (std::size_t i = 0; i < inv.items.size(); ++i)
                {
                    const auto& it = inv.items[i];
                    const ItemDef* def = reg.find(it.config_path);
                    const char* name =
                        (def != nullptr) ? def->name.c_str() : it.config_path.c_str();
                    if (it.quantity > 1)
                        ImGui::Text("- %s x%d", name, it.quantity);
                    else
                        ImGui::Text("- %s", name);
                }
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Equipment"))
        {
            ui.menu_tab = UIState::Tab::Equipment;
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
    drawHintBar("[Esc/RMB] Resume", ImVec2(460, 460));

    return quit;
}

// ---------------------------------------------------------------------------
// Pause-menu open/close trigger. ESC during Playing opens the pause
// overlay. ESC or RMB closes it (RMB doubles as the back gesture inside
// any menu - matches prison-escape's pattern). Suppressed for one frame
// after the overlay closes to keep the game from immediately re-pausing.
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
    else if (esc || rmb)
    {
        ui.active_screen = UIState::Screen::None;
        ui.input_suppressed = true;
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

    // Also: if we're in pause overlay, release mouse; if we close pause,
    // re-capture.
    auto& gs = gameState();
    auto& ui = uiState();
    if (gs.phase == GameState::Phase::Playing)
    {
        const bool want_relative = !ui.isScreenOpen();
        const bool is_relative = (SDL_GetRelativeMouseMode() == SDL_TRUE);
        if (want_relative != is_relative)
        {
            SDL_SetRelativeMouseMode(want_relative ? SDL_TRUE : SDL_FALSE);
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
    case GameState::Phase::CharCreate:
        renderCharCreate();
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
