#include "screens/LoadGameScreen.h"

#include "SaveManager.h"
#include "UIRenderer.h"
#include "ecs/EntityManager.h"
#include "ecs/GameConfig.h"
#include "screens/ConfirmDialog.h"
#include "screens/ScreenColors.h"
#include "screens/ScreenInput.h"
#include "systems/AudioSystem.h"

#include <tracy/Tracy.hpp>

#include <SDL.h>

#include <algorithm>
#include <string>

using screen_input::keyPressed;
using screen_input::mouseClicked;
using namespace screen_colors;

static FontHandle sBodyFont = INVALID_FONT;
static FontHandle sTitleFont = INVALID_FONT;
static FontHandle sBigTitleFont = INVALID_FONT;
static int sSel = -1;
static std::string sSelectedName;

// Confirmation dialog state.
static bool sConfirmDelete = false;
static int sDeleteTarget = 0;
static int sConfirmSel = 1; // 0 = Yes, 1 = No (default to No for safety)

static constexpr Color TITLE_COLOR{0.9f, 0.78f, 0.45f, 1.0f};
static constexpr Color SELECTED_BG{0.25f, 0.22f, 0.38f, 0.6f};
static constexpr Color HOVER_BG{0.15f, 0.13f, 0.22f, 0.4f};
static constexpr Color DELETE_BG{0.4f, 0.08f, 0.08f, 0.6f};
static constexpr Color DELETE_BG_HL{0.6f, 0.12f, 0.12f, 0.8f};
static constexpr Color MONEY_GREEN{0.35f, 0.82f, 0.35f, 1.0f};

static void playSfx(const SoundConfig& snd)
{
    if (!snd.get("ui_click").path.empty())
        AudioSystem::playSfx(snd.get("ui_click").path, snd.get("ui_click").volume);
}

static bool handleDeleteConfirmation(EntityManager& em, SaveData& saveData, const SoundConfig& snd,
                                     float ww, float wh)
{
    const int charCount = static_cast<int>(saveData.characters.size());
    if (sDeleteTarget < charCount)
    {
        const std::string& charName = saveData.characters[static_cast<size_t>(sDeleteTarget)].name;

        ConfirmDialog::Options opts;
        opts.title_font = sTitleFont;
        opts.body_font = sBodyFont;
        opts.title = "Delete " + charName + "?";
        opts.body_lines = {"This will also remove their run history."};
        opts.selection = &sConfirmSel;

        auto dlgResult = ConfirmDialog::render(em, opts, ww, wh);
        if (dlgResult == ConfirmDialog::Result::Yes)
        {
            SaveManager::deleteCharacter(saveData, charName);
            SaveManager::save(saveData);
            sConfirmDelete = false;
            const int newCount = static_cast<int>(saveData.characters.size());
            if (sSel >= newCount && newCount > 0)
                sSel = newCount - 1;
            playSfx(snd);
        }
        else if (dlgResult == ConfirmDialog::Result::No)
        {
            sConfirmDelete = false;
            playSfx(snd);
        }
    }
    else
    {
        sConfirmDelete = false;
    }
    return true;
}

struct RowLayout
{
    float lx, list_w, row_h, row_pad;
    float edit_w, edit_h;
    float del_w, del_h;
    TextSize xsz;
    float mx, my;
};

static void drawCharacterRow(EntityManager& em, const SaveData& saveData, const SoundConfig& snd,
                             int i, float ly, const RowLayout& lay, LoadGameScreen::Action& result,
                             bool& anyHovered)
{
    const bool selected = (i == sSel);

    // Clickable row area excludes the Edit and Delete buttons.
    const float buttons_w = lay.edit_w + 8.0f + lay.del_w + 12.0f;
    const float row_content_w = lay.list_w - buttons_w;
    const bool rowHovered = lay.mx >= lay.lx && lay.mx < lay.lx + row_content_w && lay.my >= ly &&
                            lay.my < ly + lay.row_h;
    if (rowHovered)
        anyHovered = true;

    if (selected)
        UIRenderer::drawRect(lay.lx, ly, lay.list_w, lay.row_h, SELECTED_BG);
    else if (rowHovered)
        UIRenderer::drawRect(lay.lx, ly, lay.list_w, lay.row_h, HOVER_BG);

    const auto& prof = saveData.characters[static_cast<size_t>(i)];
    const bool highlighted = selected || rowHovered;
    UIRenderer::drawText(sTitleFont, prof.name, lay.lx + 16.0f, ly + lay.row_pad,
                         highlighted ? TEXT_WHITE : TEXT_DIM);

    if (prof.money > 0)
    {
        const std::string moneyStr = "Money: " + std::to_string(prof.money);
        const TextSize mtsz = UIRenderer::measureText(sBodyFont, moneyStr);
        const float money_x = lay.lx + row_content_w - 8.0f - mtsz.width;
        const float money_y = ly + (lay.row_h - mtsz.height) * 0.5f;
        UIRenderer::drawText(sBodyFont, moneyStr, money_x, money_y, MONEY_GREEN);
    }

    // "Edit" button (edit appearance).
    const float edit_x = lay.lx + lay.list_w - lay.del_w - 8.0f - lay.edit_w - 4.0f;
    const float edit_y = ly + (lay.row_h - lay.edit_h) * 0.5f;
    const bool editHovered = lay.mx >= edit_x && lay.mx < edit_x + lay.edit_w && lay.my >= edit_y &&
                             lay.my < edit_y + lay.edit_h;
    if (editHovered)
        anyHovered = true;

    static constexpr Color EDIT_BG{0.15f, 0.15f, 0.3f, 0.6f};
    static constexpr Color EDIT_BG_HL{0.25f, 0.25f, 0.45f, 0.8f};
    UIRenderer::drawRect(edit_x, edit_y, lay.edit_w, lay.edit_h,
                         editHovered ? EDIT_BG_HL : EDIT_BG);
    const TextSize esz = UIRenderer::measureText(sBodyFont, "Edit");
    UIRenderer::drawText(sBodyFont, "Edit", edit_x + (lay.edit_w - esz.width) * 0.5f,
                         edit_y + (lay.edit_h - esz.height) * 0.5f,
                         editHovered ? TEXT_WHITE : BTN_NORMAL);

    if (editHovered && mouseClicked(em, SDL_BUTTON_LEFT))
    {
        sSelectedName = prof.name;
        result = LoadGameScreen::Action::EditLook;
        playSfx(snd);
    }

    // "X" delete button.
    const float del_x = lay.lx + lay.list_w - lay.del_w - 4.0f;
    const float del_y = ly + (lay.row_h - lay.del_h) * 0.5f;
    const bool delHovered = lay.mx >= del_x && lay.mx < del_x + lay.del_w && lay.my >= del_y &&
                            lay.my < del_y + lay.del_h;

    if (delHovered)
        anyHovered = true;

    UIRenderer::drawRect(del_x, del_y, lay.del_w, lay.del_h, delHovered ? DELETE_BG_HL : DELETE_BG);
    UIRenderer::drawText(sBodyFont, "X", del_x + 8.0f, del_y + (lay.del_h - lay.xsz.height) * 0.5f,
                         delHovered ? TEXT_WHITE : BTN_NORMAL);

    if (delHovered && mouseClicked(em, SDL_BUTTON_LEFT))
    {
        sDeleteTarget = i;
        sConfirmDelete = true;
        sConfirmSel = 1;
        playSfx(snd);
    }

    if (rowHovered && mouseClicked(em, SDL_BUTTON_LEFT))
    {
        sSel = i;
        sSelectedName = prof.name;
        result = LoadGameScreen::Action::Select;
        playSfx(snd);
    }
}

void LoadGameScreen::init(FontHandle body_font, FontHandle title_font, FontHandle big_title_font)
{
    sBodyFont = body_font;
    sTitleFont = title_font;
    sBigTitleFont = big_title_font;
}

void LoadGameScreen::reset()
{
    sSel = -1;
    sSelectedName.clear();
    sConfirmDelete = false;
    sDeleteTarget = 0;
    sConfirmSel = 1;
}

const char* LoadGameScreen::getSelectedName()
{
    return sSelectedName.c_str();
}

// Handle keyboard input and return any action triggered.
static LoadGameScreen::Action handleLoadInput(EntityManager& em, const SoundConfig& snd,
                                              const SaveData& saveData)
{
    const int charCount = static_cast<int>(saveData.characters.size());
    const int totalItems = charCount + 1;

    if (keyPressed(em, SDL_SCANCODE_UP) || keyPressed(em, SDL_SCANCODE_W))
        sSel = sSel < 0 ? 0 : (sSel - 1 + totalItems) % totalItems;
    if (keyPressed(em, SDL_SCANCODE_DOWN) || keyPressed(em, SDL_SCANCODE_S))
        sSel = sSel < 0 ? 0 : (sSel + 1) % totalItems;

    if (keyPressed(em, SDL_SCANCODE_ESCAPE) || mouseClicked(em, SDL_BUTTON_RIGHT))
    {
        screen_input::playClickSfx(em);
        return LoadGameScreen::Action::Back;
    }

    // Delete key opens confirmation for the selected character.
    if (keyPressed(em, SDL_SCANCODE_DELETE) && sSel >= 0 && sSel < charCount)
    {
        sDeleteTarget = sSel;
        sConfirmDelete = true;
        sConfirmSel = 1;
        playSfx(snd);
        return LoadGameScreen::Action::None;
    }

    if (sSel < 0 ||
        (!keyPressed(em, SDL_SCANCODE_RETURN) && !keyPressed(em, SDL_SCANCODE_KP_ENTER)))
        return LoadGameScreen::Action::None;

    LoadGameScreen::Action result = LoadGameScreen::Action::None;
    if (sSel < charCount)
    {
        sSelectedName = saveData.characters[static_cast<size_t>(sSel)].name;
        result = LoadGameScreen::Action::Select;
    }
    else
    {
        result = LoadGameScreen::Action::Back;
    }
    playSfx(snd);
    return result;
}

LoadGameScreen::Action LoadGameScreen::render(EntityManager& em, int window_w, int window_h)
{
    ZoneScopedN("LoadGameScreen");

    const float ww = static_cast<float>(window_w);
    const float wh = static_cast<float>(window_h);
    auto& saveData = em.registry().ctx().get<SaveData>();
    const auto& snd = em.registry().ctx().get<SoundConfig>();
    const int charCount = static_cast<int>(saveData.characters.size());

    Action result = Action::None;

    if (sConfirmDelete)
    {
        handleDeleteConfirmation(em, saveData, snd, ww, wh);
        return result;
    }

    result = handleLoadInput(em, snd, saveData);

    UIRenderer::drawRect(0.0f, 0.0f, ww, wh, OVERLAY_OPAQUE);

    const std::string title = "Load Game";
    const TextSize tsz = UIRenderer::measureText(sBigTitleFont, title);
    UIRenderer::drawText(sBigTitleFont, title, (ww - tsz.width) * 0.5f, wh * 0.12f, TITLE_COLOR);

    const std::string subtitle = "Select a character";
    const TextSize ssz = UIRenderer::measureText(sBodyFont, subtitle);
    const float subtitle_y = wh * 0.12f + FontManager::lineHeight(sBigTitleFont) + 8.0f;
    UIRenderer::drawText(sBodyFont, subtitle, (ww - ssz.width) * 0.5f, subtitle_y, TEXT_DIM);

    int mouseX = 0;
    int mouseY = 0;
    SDL_GetMouseState(&mouseX, &mouseY);
    const float mx = static_cast<float>(mouseX);
    const float my = static_cast<float>(mouseY);

    const float row_pad = 12.0f;
    const float row_h = FontManager::lineHeight(sTitleFont) + row_pad * 2.0f;
    const float list_w = 460.0f;
    const float list_h = static_cast<float>(charCount) * (row_h + 6.0f) + 40.0f;
    const float panel_h = list_h + row_h + 60.0f;
    const float lx = (ww - list_w) * 0.5f;
    const float panel_y = wh * 0.32f;

    UIRenderer::drawRect(lx - 20.0f, panel_y - 20.0f, list_w + 40.0f, panel_h, PANEL_BG);

    const TextSize xsz = UIRenderer::measureText(sBodyFont, "X");
    const float del_w = xsz.width + 16.0f;
    const float del_h = xsz.height + 10.0f;
    const TextSize editSz = UIRenderer::measureText(sBodyFont, "Edit");
    const float edit_w = editSz.width + 16.0f;
    const float edit_h = editSz.height + 10.0f;
    const RowLayout rowLay{lx, list_w, row_h, row_pad, edit_w, edit_h, del_w, del_h, xsz, mx, my};

    float ly = panel_y;
    bool anyHovered = false;
    for (int i = 0; i < charCount; ++i)
    {
        drawCharacterRow(em, saveData, snd, i, ly, rowLay, result, anyHovered);
        ly += row_h + 6.0f;
    }

    // Back button.
    const std::string backLabel = "Back";
    const TextSize bsz = UIRenderer::measureText(sTitleFont, backLabel);
    const float bw = bsz.width + 60.0f;
    const float bh = bsz.height + 20.0f;
    const float bx = (ww - bw) * 0.5f;
    const float visual_bottom = panel_y - 20.0f + panel_h;
    ly = visual_bottom - 20.0f - bh;

    const bool backHover = mx >= bx && mx < bx + bw && my >= ly && my < ly + bh;
    const bool backSel = (sSel == charCount);
    const bool backHighlighted = backSel || backHover;

    UIRenderer::drawRect(bx, ly, bw, bh, backSel ? BTN_BG_HL : (backHover ? HOVERED_BG : BTN_BG));
    UIRenderer::drawText(sTitleFont, backLabel, bx + 30.0f, ly + 10.0f,
                         backHighlighted ? BTN_HOVER : BTN_NORMAL);

    if (backHover && mouseClicked(em, SDL_BUTTON_LEFT))
    {
        sSel = charCount;
        result = Action::Back;
        playSfx(snd);
    }

    return result;
}
