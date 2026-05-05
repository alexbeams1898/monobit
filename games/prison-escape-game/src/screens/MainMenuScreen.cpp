#include "screens/MainMenuScreen.h"

#include "UIRenderer.h"
#include "Version.h"
#include "ecs/EntityManager.h"
#include "ecs/GameConfig.h"
#include "ops/UpdateChecker.h"
#include "screens/ConfirmDialog.h"
#include "screens/ScreenColors.h"
#include "screens/ScreenInput.h"
#include "systems/AudioSystem.h"

#include <SDL.h>
#include <algorithm>
#include <cstdlib>
#include <string>
#include <tracy/Tracy.hpp>

using screen_input::keyPressed;
using screen_input::mouseClicked;
using namespace screen_colors;

static FontHandle sBodyFont = INVALID_FONT;
static FontHandle sTitleFont = INVALID_FONT;
static FontHandle sBigTitleFont = INVALID_FONT;
static int sSel = -1;
static int sHovered = -1;

static constexpr Color TITLE_COLOR{0.9f, 0.78f, 0.45f, 1.0f};

void MainMenuScreen::init(FontHandle body_font, FontHandle title_font, FontHandle big_title_font)
{
    sBodyFont = body_font;
    sTitleFont = title_font;
    sBigTitleFont = big_title_font;
}

void MainMenuScreen::reset()
{
    sSel = -1;
    sHovered = -1;
}

static MainMenuScreen::Action drawMenuButtons(EntityManager& em, const char* const* labels,
                                              const MainMenuScreen::Action* actions, int btnCount,
                                              const SoundConfig& snd, float ww, float wh)
{
    MainMenuScreen::Action result = MainMenuScreen::Action::None;

    int mouseX = 0;
    int mouseY = 0;
    SDL_GetMouseState(&mouseX, &mouseY);
    const float mx = static_cast<float>(mouseX);
    const float my = static_cast<float>(mouseY);

    const float btn_pad_x = 30.0f;
    const float btn_pad_y = 10.0f;
    const float btn_gap = 12.0f;
    const float line_h = FontManager::lineHeight(sTitleFont);
    const float btn_h = line_h + btn_pad_y * 2.0f;
    const float total_h =
        static_cast<float>(btnCount) * btn_h + static_cast<float>(btnCount - 1) * btn_gap;
    float by = (wh - total_h) * 0.5f + wh * 0.05f;

    sHovered = -1;
    for (int i = 0; i < btnCount; ++i)
    {
        const std::string label = labels[i];
        const TextSize sz = UIRenderer::measureText(sTitleFont, label);
        const float bw = sz.width + btn_pad_x * 2.0f;
        const float bx = (ww - bw) * 0.5f;

        const bool hovered = (mx >= bx && mx < bx + bw && my >= by && my < by + btn_h);
        if (hovered)
            sHovered = i;

        if (hovered && mouseClicked(em, SDL_BUTTON_LEFT))
        {
            sSel = i;
            result = actions[i];
            if (result != MainMenuScreen::Action::None && !snd.get("ui_click").path.empty())
                AudioSystem::playSfx(snd.get("ui_click").path, snd.get("ui_click").volume);
        }

        const bool selected = (i == sSel);
        const bool highlighted = selected || hovered;
        UIRenderer::drawRect(bx, by, bw, btn_h,
                             selected ? BTN_BG_HL : (hovered ? HOVERED_BG : BTN_BG));
        UIRenderer::drawText(sTitleFont, label, bx + btn_pad_x, by + btn_pad_y,
                             highlighted ? BTN_HOVER : BTN_NORMAL);

        by += btn_h + btn_gap;
    }

    return result;
}

static MainMenuScreen::Action handleKeyboardInput(const EntityManager& em, const SoundConfig& snd,
                                                  const MainMenuScreen::Action* actions,
                                                  int btnCount)
{
    using Action = MainMenuScreen::Action;

    if (keyPressed(em, SDL_SCANCODE_UP) || keyPressed(em, SDL_SCANCODE_W))
        sSel = sSel < 0 ? 0 : (sSel - 1 + btnCount) % btnCount;
    if (keyPressed(em, SDL_SCANCODE_DOWN) || keyPressed(em, SDL_SCANCODE_S))
        sSel = sSel < 0 ? 0 : (sSel + 1) % btnCount;

    if (sSel >= 0 && (keyPressed(em, SDL_SCANCODE_RETURN) || keyPressed(em, SDL_SCANCODE_KP_ENTER)))
    {
        const Action result = actions[sSel];
        if (result != Action::None && !snd.get("ui_click").path.empty())
            AudioSystem::playSfx(snd.get("ui_click").path, snd.get("ui_click").volume);
        return result;
    }
    return Action::None;
}

// ---------------------------------------------------------------------------
// Update progress overlay
// ---------------------------------------------------------------------------

static void renderInstallButton(EntityManager& em, float px, float panelW, float btnY)
{
    int mouseX = 0;
    int mouseY = 0;
    SDL_GetMouseState(&mouseX, &mouseY);
    const float mx = static_cast<float>(mouseX);
    const float my = static_cast<float>(mouseY);

    const std::string label = "Install & Restart";
    const TextSize bsz = UIRenderer::measureText(sTitleFont, label);
    const float bw = bsz.width + 60.0f;
    const float bh = bsz.height + 20.0f;
    const float bx = px + (panelW - bw) * 0.5f;

    const bool hovered = (mx >= bx && mx < bx + bw && my >= btnY && my < btnY + bh);
    UIRenderer::drawRect(bx, btnY, bw, bh, hovered ? BTN_BG_HL : BTN_BG);
    UIRenderer::drawText(sTitleFont, label, bx + 30.0f, btnY + 10.0f,
                         hovered ? BTN_HOVER : BTN_NORMAL);

    if (hovered && mouseClicked(em, SDL_BUTTON_LEFT))
    {
        if (UpdateChecker::installAndRelaunch())
            std::exit(0); // NOLINT(concurrency-mt-unsafe) -- intentional immediate exit
    }
}

static void renderFailedButtons(EntityManager& em, float px, float panelW, float btnY)
{
    int mouseX = 0;
    int mouseY = 0;
    SDL_GetMouseState(&mouseX, &mouseY);
    const float mx = static_cast<float>(mouseX);
    const float my = static_cast<float>(mouseY);

    const std::string retryLabel = "Retry";
    const TextSize rsz = UIRenderer::measureText(sTitleFont, retryLabel);
    const float rw = rsz.width + 60.0f;
    const float rh = rsz.height + 20.0f;

    const std::string closeLabel = "Close";
    const TextSize csz = UIRenderer::measureText(sTitleFont, closeLabel);
    const float cw = csz.width + 60.0f;

    const float gap = 20.0f;
    const float totalW = rw + gap + cw;
    const float rx = px + (panelW - totalW) * 0.5f;
    const float cx = rx + rw + gap;

    const bool retryHov = (mx >= rx && mx < rx + rw && my >= btnY && my < btnY + rh);
    UIRenderer::drawRect(rx, btnY, rw, rh, retryHov ? BTN_BG_HL : BTN_BG);
    UIRenderer::drawText(sTitleFont, retryLabel, rx + 30.0f, btnY + 10.0f,
                         retryHov ? BTN_HOVER : BTN_NORMAL);
    if (retryHov && mouseClicked(em, SDL_BUTTON_LEFT))
    {
        UpdateChecker::resetState();
        UpdateChecker::startDownload();
    }

    const bool closeHov = (mx >= cx && mx < cx + cw && my >= btnY && my < btnY + rh);
    UIRenderer::drawRect(cx, btnY, cw, rh, closeHov ? BTN_BG_HL : BTN_BG);
    UIRenderer::drawText(sTitleFont, closeLabel, cx + 30.0f, btnY + 10.0f,
                         closeHov ? BTN_HOVER : BTN_NORMAL);
    if (closeHov && mouseClicked(em, SDL_BUTTON_LEFT))
        UpdateChecker::resetState();
}

static void renderUpdateProgress(EntityManager& em, float ww, float wh)
{
    using UState = UpdateChecker::UpdateState;
    const auto ustate = UpdateChecker::updateState();

    const std::string statusText = UpdateChecker::statusMessage();
    const TextSize sts = UIRenderer::measureText(sBodyFont, statusText);

    constexpr float barW = 400.0f;
    constexpr float barH = 16.0f;
    constexpr float pad = 28.0f;
    const float panelW = std::max(barW, sts.width) + pad * 2.0f;

    const bool hasButton = (ustate == UState::ReadyToInstall || ustate == UState::Failed);
    const float btnRowH = hasButton ? 50.0f : 0.0f;
    const float panelH = pad + sts.height + pad + barH + btnRowH + pad;
    const float px = (ww - panelW) * 0.5f;
    const float py = (wh - panelH) * 0.5f;

    UIRenderer::drawRect(0.0f, 0.0f, ww, wh, OVERLAY);
    UIRenderer::drawRect(px, py, panelW, panelH, PANEL_BG);

    UIRenderer::drawText(sBodyFont, statusText, px + (panelW - sts.width) * 0.5f, py + pad,
                         TEXT_WHITE);

    const float barX = px + (panelW - barW) * 0.5f;
    const float barY = py + pad + sts.height + pad;
    static constexpr Color PROGRESS_BG{0.15f, 0.15f, 0.18f, 0.8f};
    static constexpr Color PROGRESS_FILL{0.3f, 0.7f, 0.3f, 0.9f};
    static constexpr Color PROGRESS_EXTRACT{0.3f, 0.5f, 0.7f, 0.6f};

    UIRenderer::drawRect(barX, barY, barW, barH, PROGRESS_BG);
    if (ustate == UState::Downloading)
    {
        const float progress = UpdateChecker::downloadProgress();
        if (progress >= 0.0f)
            UIRenderer::drawRect(barX, barY, barW * std::min(progress, 1.0f), barH, PROGRESS_FILL);
    }
    else if (ustate == UState::Extracting)
    {
        UIRenderer::drawRect(barX, barY, barW, barH, PROGRESS_EXTRACT);
    }
    else if (ustate == UState::ReadyToInstall)
    {
        UIRenderer::drawRect(barX, barY, barW, barH, PROGRESS_FILL);
    }

    if (!hasButton)
        return;

    const float btnY = barY + barH + 14.0f;
    if (ustate == UState::ReadyToInstall)
        renderInstallButton(em, px, panelW, btnY);
    else if (ustate == UState::Failed)
        renderFailedButtons(em, px, panelW, btnY);
}

// ---------------------------------------------------------------------------
// Main render
// ---------------------------------------------------------------------------

MainMenuScreen::Action MainMenuScreen::render(EntityManager& em, int window_w, int window_h)
{
    ZoneScopedN("MainMenuScreen");

    const float ww = static_cast<float>(window_w);
    const float wh = static_cast<float>(window_h);
    const auto& saveData = em.registry().ctx().get<SaveData>();
    const auto& snd = em.registry().ctx().get<SoundConfig>();

    const bool hasChars = !saveData.characters.empty();

    static constexpr Action kActionsWithLoad[] = {Action::NewGame,    Action::LoadGame,
                                                  Action::HighScores, Action::Controls,
                                                  Action::Settings,   Action::Quit};
    static constexpr Action kActionsNoLoad[] = {Action::NewGame, Action::HighScores,
                                                Action::Controls, Action::Settings, Action::Quit};
    static constexpr const char* kLabelsWithLoad[] = {"New Game", "Load Game", "High Scores",
                                                      "Controls", "Settings",  "Quit"};
    static constexpr const char* kLabelsNoLoad[] = {"New Game", "High Scores", "Controls",
                                                    "Settings", "Quit"};

    const Action* actions = hasChars ? kActionsWithLoad : kActionsNoLoad;
    const char* const* labels = hasChars ? kLabelsWithLoad : kLabelsNoLoad;
    const int btnCount = hasChars ? 6 : 5;

    // Block menu input while any update UI is active.
    static bool sUpdateDialogShown = false;
    static bool sUpdateDismissed = false;
    static int sUpdateDlgSel = 0;
    const auto ustate = UpdateChecker::updateState();
    const bool uiBlocked = sUpdateDialogShown || ustate != UpdateChecker::UpdateState::Idle;

    Action result = Action::None;
    if (!uiBlocked)
        result = handleKeyboardInput(em, snd, actions, btnCount);

    UIRenderer::drawRect(0.0f, 0.0f, ww, wh, OVERLAY_OPAQUE);

    const std::string title = "PRISON ESCAPE GAME";
    const TextSize tsz = UIRenderer::measureText(sBigTitleFont, title);
    UIRenderer::drawText(sBigTitleFont, title, (ww - tsz.width) * 0.5f, wh * 0.2f, TITLE_COLOR);

    const Action btnResult =
        uiBlocked ? Action::None : drawMenuButtons(em, labels, actions, btnCount, snd, ww, wh);
    if (result == Action::None)
        result = btnResult;

    // Version label in bottom-right corner.
    const std::string versionLabel = "v" GAME_VERSION;
    const TextSize vsz = UIRenderer::measureText(sBodyFont, versionLabel);
    constexpr float margin = 20.0f;
    const float versionY = wh - vsz.height - margin;
    UIRenderer::drawText(sBodyFont, versionLabel, ww - vsz.width - margin, versionY, TEXT_DIM);

    // Background update check (fires once on first render).
    static bool sCheckStarted = false;
    if (!sCheckStarted)
    {
        UpdateChecker::startCheck();
        sCheckStarted = true;
    }

    // Update notification: dialog on first detection, then button if dismissed.
    if (UpdateChecker::updateAvailable() && !sUpdateDismissed && !sUpdateDialogShown &&
        ustate == UpdateChecker::UpdateState::Idle)
    {
        sUpdateDialogShown = true;
    }

    if (sUpdateDialogShown)
    {
        ConfirmDialog::Options opts;
        opts.title_font = sTitleFont;
        opts.body_font = sBodyFont;
        opts.title = "Update Available";
        opts.body_lines = {"v" + UpdateChecker::latestVersion() + " is available.",
                           "Download and install?"};
        opts.selection = &sUpdateDlgSel;
        opts.min_width = 320.0f;

        const auto dlgResult = ConfirmDialog::render(em, opts, ww, wh);
        if (dlgResult == ConfirmDialog::Result::Yes)
        {
            sUpdateDialogShown = false;
            sUpdateDismissed = true;
            UpdateChecker::startDownload();
        }
        else if (dlgResult == ConfirmDialog::Result::No)
        {
            sUpdateDialogShown = false;
            sUpdateDismissed = true;
        }
    }
    else if (UpdateChecker::updateAvailable() && sUpdateDismissed &&
             ustate == UpdateChecker::UpdateState::Idle)
    {
        // Persistent "Update" button above version label.
        static constexpr Color UPDATE_BTN_COLOR{0.9f, 0.78f, 0.45f, 1.0f};
        const std::string updateLabel = "Update to v" + UpdateChecker::latestVersion();
        const TextSize usz = UIRenderer::measureText(sBodyFont, updateLabel);
        const float btn_pad_x = 12.0f;
        const float btn_pad_y = 6.0f;
        const float ubw = usz.width + btn_pad_x * 2.0f;
        const float ubh = usz.height + btn_pad_y * 2.0f;
        const float ubx = ww - ubw - margin;
        const float uby = versionY - ubh - 6.0f;

        int mouseX = 0;
        int mouseY = 0;
        SDL_GetMouseState(&mouseX, &mouseY);
        const float fmx = static_cast<float>(mouseX);
        const float fmy = static_cast<float>(mouseY);
        const bool hovered = (fmx >= ubx && fmx < ubx + ubw && fmy >= uby && fmy < uby + ubh);

        UIRenderer::drawRect(ubx, uby, ubw, ubh, hovered ? BTN_BG_HL : BTN_BG);
        UIRenderer::drawText(sBodyFont, updateLabel, ubx + btn_pad_x, uby + btn_pad_y,
                             hovered ? UPDATE_BTN_COLOR : BTN_NORMAL);

        if (hovered && mouseClicked(em, SDL_BUTTON_LEFT))
            UpdateChecker::startDownload();
    }

    // Progress overlay while downloading / extracting / ready to install.
    if (ustate != UpdateChecker::UpdateState::Idle)
        renderUpdateProgress(em, ww, wh);

    return result;
}
