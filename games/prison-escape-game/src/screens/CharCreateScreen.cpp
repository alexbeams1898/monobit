#include "screens/CharCreateScreen.h"

#include "SpriteCompositor.h"
#include "UIRenderer.h"
#include "ecs/AppearanceConfig.h"
#include "ecs/EntityManager.h"
#include "ecs/GameConfig.h"
#include "ops/AppearanceOps.h"
#include "screens/ScreenColors.h"
#include "screens/ScreenInput.h"
#include "systems/AudioSystem.h"

#include <tracy/Tracy.hpp>

#include <SDL.h>

#include <cmath>
#include <cstdio>
#include <string>

using screen_input::keyPressed;
using screen_input::mouseClicked;
using namespace screen_colors;

static FontHandle sBodyFont = INVALID_FONT;
static FontHandle sTitleFont = INVALID_FONT;
static std::string sName;
static bool sEditMode = false;
static constexpr int MAX_NAME_LEN = 20;

// Per-category selection index (indexed by category index in AppearanceConfig).
static std::vector<int> sCatSelection;
// Per-category slider value (only meaningful for Slider categories; 0 otherwise).
static std::vector<float> sCatSliderValue;
static std::unordered_map<std::string, std::string> sCurrentSelections;
static std::unordered_map<std::string, std::string> sPendingAppearance;

// Slider drag state (mouse held on a slider track).
static int sSliderDragCatIdx = -1;

// Preview.
static uint32_t sPreviewTex = 0;
static int sPreviewTexW = 0;
static int sPreviewTexH = 0;
static float sAnimTimer = 0.0f;
static int sAnimFrame = 0;
static uint32_t sLastTicks = 0;

// Focus: -1=name, 0..visibleCount-1=visible categories, visibleCount=confirm,
// visibleCount+1=back.
static int sFocusRow = -1;
static bool sInitialized = false;

// Visible category indices (excludes linked_to and suppressed color categories).
static std::vector<int> sVisibleCats;

static constexpr Color TITLE_COLOR{0.9f, 0.78f, 0.45f, 1.0f};
static constexpr Color FIELD_BG{0.12f, 0.12f, 0.15f, 0.9f};
static constexpr Color FIELD_BORDER{0.5f, 0.45f, 0.3f, 0.8f};
static constexpr Color CURSOR_COLOR{0.9f, 0.78f, 0.45f, 1.0f};
static constexpr Color BTN_DIM{0.35f, 0.35f, 0.35f, 0.6f};
static constexpr Color ARROW_COLOR{0.7f, 0.65f, 0.5f, 1.0f};
static constexpr Color ARROW_HL{0.95f, 0.88f, 0.55f, 1.0f};
static constexpr Color LABEL_COLOR{0.6f, 0.58f, 0.54f, 1.0f};
static constexpr Color VALUE_COLOR{0.92f, 0.90f, 0.88f, 1.0f};

static constexpr float WALK_FRAME_DURATION = 0.1f;
static constexpr int PREVIEW_SCALE = 3;
static constexpr int NUM_DIRS = 4;
static constexpr int FRAME_PX = 64;
static constexpr int WALK_FRAMES = 8; // walk row frame count (matches lpc_humanoid.json)
static constexpr float SWATCH_SIZE = 22.0f;
static constexpr float SWATCH_GAP = 6.0f;
static constexpr float SWATCH_BORDER = 2.0f;
static constexpr Color SWATCH_SEL_BORDER{0.95f, 0.88f, 0.55f, 1.0f};

// Content layout. Two columns below the preview; each column owns half the
// panel width and lays out its categories independently.
// LABEL_GAP adds breathing room between the label column and the value so the
// text never crowds the selectors.
static constexpr float CONTENT_W = 1040.0f;
static constexpr float COL_GAP = 60.0f;
static constexpr float LABEL_COL_W = 220.0f;
static constexpr float LABEL_GAP = 40.0f;
static constexpr float VALUE_COL_X = LABEL_COL_W + LABEL_GAP;
static constexpr float ROW_V_PAD = 22.0f;
static constexpr int MAX_SWATCHES_PER_ROW = 6;

static constexpr float SLIDER_TRACK_W = 150.0f;
static constexpr float SLIDER_TRACK_H = 6.0f;
static constexpr float SLIDER_HANDLE_W = 8.0f;
static constexpr float SLIDER_HANDLE_H = 18.0f;
static constexpr Color SLIDER_TRACK_COLOR{0.25f, 0.23f, 0.20f, 1.0f};
static constexpr Color SLIDER_FILL_COLOR{0.7f, 0.65f, 0.5f, 1.0f};
static constexpr Color SLIDER_HANDLE_COLOR{0.92f, 0.90f, 0.88f, 1.0f};
static constexpr Color SLIDER_HANDLE_HL{0.95f, 0.88f, 0.55f, 1.0f};

static bool isNameValid()
{
    for (const char c : sName)
        if (c != ' ')
            return true;
    return false;
}

static bool hasSwatch(const AppearanceCategory& cat)
{
    for (const auto& opt : cat.options)
        if (opt.swatch != 0)
            return true;
    return false;
}

static Color swatchToColor(uint32_t swatch)
{
    const float r = static_cast<float>((swatch >> 24) & 0xFF) / 255.0f;
    const float g = static_cast<float>((swatch >> 16) & 0xFF) / 255.0f;
    const float b = static_cast<float>((swatch >> 8) & 0xFF) / 255.0f;
    return {r, g, b, 1.0f};
}

static std::string getCatSelection(const AppearanceConfig& cfg, int catIdx)
{
    if (catIdx < 0 || catIdx >= static_cast<int>(cfg.categories.size()))
        return {};
    if (catIdx >= static_cast<int>(sCatSelection.size()))
        return {};
    const auto& cat = cfg.categories[catIdx];
    const int idx = sCatSelection[catIdx];
    if (idx >= 0 && idx < static_cast<int>(cat.options.size()))
        return cat.options[idx].id;
    return {};
}

static float clampSliderValue(const AppearanceCategory& cat, float value)
{
    if (value < cat.min_value)
        return cat.min_value;
    if (value > cat.max_value)
        return cat.max_value;
    return value;
}

static float snapSliderValue(const AppearanceCategory& cat, float value)
{
    if (cat.step_value <= 0.0f)
        return clampSliderValue(cat, value);
    const float steps = (value - cat.min_value) / cat.step_value;
    const float snapped = cat.min_value + std::round(steps) * cat.step_value;
    return clampSliderValue(cat, snapped);
}

static std::string formatSliderValue(float value)
{
    // "105%" style.
    const int pct = static_cast<int>(std::round(value * 100.0f));
    return std::to_string(pct) + "%";
}

static std::string sliderValueString(float value)
{
    // Stored form for SaveData: 2 decimal places.
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%.2f", value);
    return buf;
}

static std::unordered_map<std::string, std::string> buildSelections(const AppearanceConfig& cfg)
{
    std::unordered_map<std::string, std::string> sel;
    for (size_t i = 0; i < cfg.categories.size() && i < sCatSelection.size(); ++i)
    {
        const auto& cat = cfg.categories[i];
        if (cat.hidden)
            continue;
        if (cat.type == AppearanceCategoryType::Slider)
        {
            const float v = (i < sCatSliderValue.size()) ? sCatSliderValue[i] : cat.default_value;
            sel[cat.id] = sliderValueString(v);
        }
        else
        {
            sel[cat.id] = getCatSelection(cfg, static_cast<int>(i));
        }
    }
    return sel;
}

// Build the list of visible category indices, skipping linked and suppressed.
static void rebuildVisibleCats(const AppearanceConfig& cfg)
{
    sVisibleCats.clear();
    for (int i = 0; i < static_cast<int>(cfg.categories.size()); ++i)
    {
        const auto& cat = cfg.categories[i];

        // Hidden and linked categories are not shown in the character creator.
        if (cat.hidden || !cat.linked_to.empty())
            continue;

        // Color categories are hidden when their parent style is "none".
        // Sliders never have combine_with; skip this check for them.
        if (cat.type != AppearanceCategoryType::Slider && !cat.combine_with.empty())
        {
            // Find the style category.
            for (int j = 0; j < static_cast<int>(cfg.categories.size()); ++j)
            {
                if (cfg.categories[j].id == cat.combine_with)
                {
                    const std::string parentSel = getCatSelection(cfg, j);
                    if (parentSel.empty() || parentSel == "none")
                        goto skip;
                    break;
                }
            }
        }

        sVisibleCats.push_back(i);
        continue;
    skip:;
    }
}

static void recomposite(EntityManager& em)
{
    const auto* cfg = em.registry().ctx().find<AppearanceConfig>();
    if (cfg == nullptr || !cfg->loaded || cfg->compositor == nullptr)
        return;

    auto sel = buildSelections(*cfg);
    std::vector<PaletteSwap> palettes;
    auto paths = AppearanceOps::buildLayerPaths(em, sel, &palettes);
    sPreviewTex = cfg->compositor->composite(paths, palettes);
    sPreviewTexW = 0;
    sPreviewTexH = 0;
    if (sPreviewTex != 0)
        cfg->compositor->getDimensions(sPreviewTex, sPreviewTexW, sPreviewTexH);
}

static void initSelectionsFromMap(const AppearanceConfig& cfg,
                                  const std::unordered_map<std::string, std::string>& appearance)
{
    sCatSelection.assign(cfg.categories.size(), 0);
    sCatSliderValue.assign(cfg.categories.size(), 0.0f);
    for (size_t i = 0; i < cfg.categories.size(); ++i)
    {
        const auto& cat = cfg.categories[i];

        if (cat.type == AppearanceCategoryType::Slider)
        {
            float v = cat.default_value;
            auto it = appearance.find(cat.id);
            if (it != appearance.end() && !it->second.empty())
            {
                try
                {
                    v = std::stof(it->second);
                }
                catch (const std::exception&)
                {
                    v = cat.default_value;
                }
            }
            sCatSliderValue[i] = snapSliderValue(cat, v);
            continue;
        }

        // For linked categories, default to first option.
        if (!cat.linked_to.empty())
            continue;

        auto it = appearance.find(cat.id);
        if (it == appearance.end())
            continue;
        for (int j = 0; j < static_cast<int>(cat.options.size()); ++j)
        {
            if (cat.options[j].id == it->second)
            {
                sCatSelection[i] = j;
                break;
            }
        }
    }
    rebuildVisibleCats(cfg);
}

void CharCreateScreen::init(FontHandle body_font, FontHandle title_font)
{
    sBodyFont = body_font;
    sTitleFont = title_font;
}

void CharCreateScreen::reset()
{
    sName.clear();
    sEditMode = false;
    sFocusRow = -1;
    sPreviewTex = 0;
    sPreviewTexW = 0;
    sPreviewTexH = 0;
    sAnimTimer = 0.0f;
    sAnimFrame = 0;
    sLastTicks = SDL_GetTicks();
    sCatSelection.clear();
    sCatSliderValue.clear();
    sSliderDragCatIdx = -1;
    sCurrentSelections.clear();
    sPendingAppearance.clear();
    sVisibleCats.clear();
    sInitialized = false;
    SDL_StartTextInput();
}

void CharCreateScreen::resetForEdit(const std::string& name,
                                    const std::unordered_map<std::string, std::string>& appearance)
{
    sName = name;
    sEditMode = true;
    sFocusRow = 0;
    sPreviewTex = 0;
    sPreviewTexW = 0;
    sPreviewTexH = 0;
    sAnimTimer = 0.0f;
    sAnimFrame = 0;
    sLastTicks = SDL_GetTicks();
    sCatSelection.clear();
    sCatSliderValue.clear();
    sSliderDragCatIdx = -1;
    sCurrentSelections.clear();
    sPendingAppearance = appearance;
    sVisibleCats.clear();
    sInitialized = false;
}

const char* CharCreateScreen::getName()
{
    return sName.c_str();
}

const std::unordered_map<std::string, std::string>& CharCreateScreen::getSelections()
{
    return sCurrentSelections;
}

bool CharCreateScreen::isEditMode()
{
    return sEditMode;
}

static void cycleCategory(EntityManager& em, const AppearanceConfig& cfg, int catIdx, int delta)
{
    const int optCount = static_cast<int>(cfg.categories[catIdx].options.size());
    if (optCount <= 0)
        return;
    sCatSelection[catIdx] = (sCatSelection[catIdx] + delta + optCount) % optCount;
    rebuildVisibleCats(cfg);
    recomposite(em);
    sCurrentSelections = buildSelections(cfg);
    screen_input::playClickSfx(em);
}

// Adjust a slider category by `steps` * step_value. No recomposite (pure transform).
static void stepSlider(EntityManager& em, const AppearanceConfig& cfg, int catIdx, int steps)
{
    if (catIdx < 0 || catIdx >= static_cast<int>(cfg.categories.size()))
        return;
    const auto& cat = cfg.categories[catIdx];
    if (cat.type != AppearanceCategoryType::Slider)
        return;
    if (catIdx >= static_cast<int>(sCatSliderValue.size()))
        return;
    const float v = sCatSliderValue[catIdx] + static_cast<float>(steps) * cat.step_value;
    sCatSliderValue[catIdx] = snapSliderValue(cat, v);
    sCurrentSelections = buildSelections(cfg);
    screen_input::playClickSfx(em);
}

static void setSliderToMin(EntityManager& em, const AppearanceConfig& cfg, int catIdx)
{
    if (catIdx < 0 || catIdx >= static_cast<int>(cfg.categories.size()))
        return;
    const auto& cat = cfg.categories[catIdx];
    if (cat.type != AppearanceCategoryType::Slider)
        return;
    sCatSliderValue[catIdx] = cat.min_value;
    sCurrentSelections = buildSelections(cfg);
    screen_input::playClickSfx(em);
}

static void setSliderToMax(EntityManager& em, const AppearanceConfig& cfg, int catIdx)
{
    if (catIdx < 0 || catIdx >= static_cast<int>(cfg.categories.size()))
        return;
    const auto& cat = cfg.categories[catIdx];
    if (cat.type != AppearanceCategoryType::Slider)
        return;
    sCatSliderValue[catIdx] = cat.max_value;
    sCurrentSelections = buildSelections(cfg);
    screen_input::playClickSfx(em);
}

// Convert a mouse X on the slider track to a snapped value.
static float sliderValueFromMouse(const AppearanceCategory& cat, float trackX, float mouseX)
{
    const float t = (mouseX - trackX) / SLIDER_TRACK_W;
    const float clampedT = (t < 0.0f) ? 0.0f : (t > 1.0f ? 1.0f : t);
    const float raw = cat.min_value + clampedT * (cat.max_value - cat.min_value);
    return snapSliderValue(cat, raw);
}

struct RowLayout
{
    float cx;
    float cy;
    float content_w;
    float row_h;
    float mx;
    float my;
};

// Render a swatch grid row. Returns new cy after this row.
static float renderSwatchRow(EntityManager& em, const AppearanceConfig& cfg, int vi, int catIdx,
                             bool focused, int selIdx, const RowLayout& lay)
{
    const auto& cat = cfg.categories[catIdx];
    const float cx = lay.cx;
    const float cy = lay.cy;
    const float contentW = lay.content_w;
    const float mx = lay.mx;
    const float my = lay.my;

    const float labelY = cy + 4.0f;
    UIRenderer::drawText(sBodyFont, cat.label + ":", cx, labelY, LABEL_COLOR);

    const float gridX = cx + VALUE_COL_X;
    const float lineH = FontManager::lineHeight(sBodyFont);
    const int optCount = static_cast<int>(cat.options.size());
    const int swatchCols = std::min(optCount, MAX_SWATCHES_PER_ROW);
    const int swatchRows = (optCount + MAX_SWATCHES_PER_ROW - 1) / MAX_SWATCHES_PER_ROW;
    const float gridBlockH = static_cast<float>(swatchRows) * SWATCH_SIZE +
                             static_cast<float>(swatchRows - 1) * SWATCH_GAP;
    // Vertically center the swatch block relative to the label text.
    const float gridY = cy + (lineH - gridBlockH) * 0.5f + 4.0f;
    const float totalGridW =
        static_cast<float>(swatchCols) * (SWATCH_SIZE + SWATCH_GAP) - SWATCH_GAP;
    const float actualRowH = std::max(lineH, gridBlockH) + ROW_V_PAD;

    if (focused)
        UIRenderer::drawRect(cx - 4.0f, cy - 2.0f, contentW + 8.0f, actualRowH, HOVERED_BG);

    for (int oi = 0; oi < optCount; ++oi)
    {
        const int row = oi / MAX_SWATCHES_PER_ROW;
        const int col2 = oi % MAX_SWATCHES_PER_ROW;
        const float sx = gridX + static_cast<float>(col2) * (SWATCH_SIZE + SWATCH_GAP);
        const float sy = gridY + static_cast<float>(row) * (SWATCH_SIZE + SWATCH_GAP);
        const Color col = swatchToColor(cat.options[oi].swatch);
        const bool selected = (oi == selIdx);

        if (selected)
        {
            UIRenderer::drawRect(sx - SWATCH_BORDER, sy - SWATCH_BORDER,
                                 SWATCH_SIZE + SWATCH_BORDER * 2.0f,
                                 SWATCH_SIZE + SWATCH_BORDER * 2.0f, SWATCH_SEL_BORDER);
        }
        UIRenderer::drawRect(sx, sy, SWATCH_SIZE, SWATCH_SIZE, col);

        if (mx >= sx && mx < sx + SWATCH_SIZE && my >= sy && my < sy + SWATCH_SIZE)
        {
            sFocusRow = vi;
            if (mouseClicked(em, SDL_BUTTON_LEFT) && oi != selIdx)
            {
                sCatSelection[catIdx] = oi;
                rebuildVisibleCats(cfg);
                recomposite(em);
                sCurrentSelections = buildSelections(cfg);
                screen_input::playClickSfx(em);
            }
        }
    }

    // Row hover (for keyboard nav).
    if (mx >= cx - 4.0f && mx < cx + contentW + 4.0f && my >= cy - 2.0f &&
        my < cy + actualRowH - 2.0f &&
        !(mx >= gridX && mx < gridX + totalGridW && my >= gridY && my < gridY + gridBlockH))
    {
        sFocusRow = vi;
    }

    return cy + actualRowH;
}

// Render a text-cycling row. Returns new cy after this row.
static float renderCycleRow(EntityManager& em, const AppearanceConfig& cfg, int vi, int catIdx,
                            bool focused, int selIdx, const RowLayout& lay)
{
    const auto& cat = cfg.categories[catIdx];
    const float cx = lay.cx;
    const float cy = lay.cy;
    const float contentW = lay.content_w;
    const float rowH = lay.row_h;
    const float mx = lay.mx;
    const float my = lay.my;

    if (focused)
        UIRenderer::drawRect(cx - 4.0f, cy - 2.0f, contentW + 8.0f, rowH, HOVERED_BG);

    UIRenderer::drawText(sBodyFont, cat.label + ":", cx, cy + 4.0f, LABEL_COLOR);

    const float valueX = cx + VALUE_COL_X;
    const std::string optLabel = (selIdx >= 0 && selIdx < static_cast<int>(cat.options.size()))
                                     ? cat.options[selIdx].label
                                     : "---";

    const Color arrowCol = focused ? ARROW_HL : ARROW_COLOR;
    const float arrowAdv = UIRenderer::drawText(sBodyFont, "<", valueX, cy + 4.0f, arrowCol);
    const float valX = valueX + arrowAdv + 12.0f;
    const float valAdv = UIRenderer::drawText(sBodyFont, optLabel, valX, cy + 4.0f, VALUE_COLOR);
    const float rArrowX = valX + valAdv + 12.0f;
    UIRenderer::drawText(sBodyFont, ">", rArrowX, cy + 4.0f, arrowCol);

    if (mx >= cx - 4.0f && mx < cx + contentW + 4.0f && my >= cy - 2.0f && my < cy + rowH - 2.0f)
    {
        sFocusRow = vi;
        if (mouseClicked(em, SDL_BUTTON_LEFT))
        {
            const float midX = (valueX + rArrowX) * 0.5f;
            cycleCategory(em, cfg, catIdx, (mx < midX) ? -1 : 1);
        }
    }

    return cy + rowH;
}

// Render a slider row. Returns new cy after this row.
static float renderSliderRow(EntityManager& em, const AppearanceConfig& cfg, int vi, int catIdx,
                             bool focused, const RowLayout& lay)
{
    const auto& cat = cfg.categories[catIdx];
    const float cx = lay.cx;
    const float cy = lay.cy;
    const float contentW = lay.content_w;
    const float rowH = lay.row_h;
    const float mx = lay.mx;
    const float my = lay.my;

    if (focused)
        UIRenderer::drawRect(cx - 4.0f, cy - 2.0f, contentW + 8.0f, rowH, HOVERED_BG);

    UIRenderer::drawText(sBodyFont, cat.label + ":", cx, cy + 4.0f, LABEL_COLOR);

    const float value = (catIdx < static_cast<int>(sCatSliderValue.size()))
                            ? sCatSliderValue[catIdx]
                            : cat.default_value;
    const float range = cat.max_value - cat.min_value;
    const float t = (range > 0.0f) ? (value - cat.min_value) / range : 0.0f;

    const float trackX = cx + VALUE_COL_X;
    const float trackY = cy + (rowH - SLIDER_TRACK_H) * 0.5f;

    // Track background.
    UIRenderer::drawRect(trackX, trackY, SLIDER_TRACK_W, SLIDER_TRACK_H, SLIDER_TRACK_COLOR);
    // Fill.
    UIRenderer::drawRect(trackX, trackY, SLIDER_TRACK_W * t, SLIDER_TRACK_H, SLIDER_FILL_COLOR);
    // Handle.
    const float handleX = trackX + SLIDER_TRACK_W * t - SLIDER_HANDLE_W * 0.5f;
    const float handleY = cy + (rowH - SLIDER_HANDLE_H) * 0.5f;
    const Color handleCol = focused ? SLIDER_HANDLE_HL : SLIDER_HANDLE_COLOR;
    UIRenderer::drawRect(handleX, handleY, SLIDER_HANDLE_W, SLIDER_HANDLE_H, handleCol);

    // Value label to right of track.
    const float labelX = trackX + SLIDER_TRACK_W + 10.0f;
    UIRenderer::drawText(sBodyFont, formatSliderValue(value), labelX, cy + 4.0f, VALUE_COLOR);

    // Mouse interaction: click/drag on track.
    const bool overRow =
        (mx >= cx - 4.0f && mx < cx + contentW + 4.0f && my >= cy - 2.0f && my < cy + rowH - 2.0f);
    if (overRow)
        sFocusRow = vi;

    const bool overTrack = (mx >= trackX - 6.0f && mx < trackX + SLIDER_TRACK_W + 6.0f &&
                            my >= handleY - 2.0f && my < handleY + SLIDER_HANDLE_H + 2.0f);

    if (overTrack && mouseClicked(em, SDL_BUTTON_LEFT))
    {
        sSliderDragCatIdx = catIdx;
        sCatSliderValue[catIdx] = sliderValueFromMouse(cat, trackX, mx);
        sCurrentSelections = buildSelections(cfg);
        screen_input::playClickSfx(em);
    }

    if (sSliderDragCatIdx == catIdx)
    {
        const Uint32 btn = SDL_GetMouseState(nullptr, nullptr);
        if ((btn & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0)
        {
            const float newV = sliderValueFromMouse(cat, trackX, mx);
            if (std::abs(newV - sCatSliderValue[catIdx]) >= 0.0005f)
            {
                sCatSliderValue[catIdx] = newV;
                sCurrentSelections = buildSelections(cfg);
            }
        }
        else
        {
            sSliderDragCatIdx = -1;
        }
    }

    return cy + rowH;
}

// Apply keyboard input to the left/right arrows of the focused row (either a
// slider or a text cycler).
static void handleFocusedRowArrows(EntityManager& em, const AppearanceConfig& cfg, int catIdx)
{
    const bool isSlider = cfg.categories[catIdx].type == AppearanceCategoryType::Slider;
    if (isSlider)
    {
        const SDL_Keymod mods = SDL_GetModState();
        const int coarse = ((mods & KMOD_SHIFT) != 0) ? 5 : 1;
        if (keyPressed(em, SDL_SCANCODE_LEFT))
            stepSlider(em, cfg, catIdx, -coarse);
        if (keyPressed(em, SDL_SCANCODE_RIGHT))
            stepSlider(em, cfg, catIdx, coarse);
        if (keyPressed(em, SDL_SCANCODE_HOME))
            setSliderToMin(em, cfg, catIdx);
        if (keyPressed(em, SDL_SCANCODE_END))
            setSliderToMax(em, cfg, catIdx);
    }
    else
    {
        if (keyPressed(em, SDL_SCANCODE_LEFT))
            cycleCategory(em, cfg, catIdx, -1);
        if (keyPressed(em, SDL_SCANCODE_RIGHT))
            cycleCategory(em, cfg, catIdx, 1);
    }
}

static void handleNameTextInput(EntityManager& em)
{
    for (const char c : em.text_input_buffer)
    {
        if (static_cast<int>(sName.size()) < MAX_NAME_LEN && c >= 32)
            sName += c;
    }
    if (keyPressed(em, SDL_SCANCODE_BACKSPACE) && !sName.empty())
        sName.pop_back();
}

static void handleVerticalNavigation(EntityManager& em, int firstRow, int backRow)
{
    if (keyPressed(em, SDL_SCANCODE_DOWN) || keyPressed(em, SDL_SCANCODE_TAB))
        sFocusRow = (sFocusRow < backRow) ? sFocusRow + 1 : firstRow;
    if (keyPressed(em, SDL_SCANCODE_UP))
        sFocusRow = (sFocusRow > firstRow) ? sFocusRow - 1 : backRow;
}

static CharCreateScreen::Action confirmActionForRow()
{
    if (sEditMode)
        return CharCreateScreen::Action::Apply;
    if (isNameValid())
        return CharCreateScreen::Action::Start;
    return CharCreateScreen::Action::None;
}

static CharCreateScreen::Action handleEnterKey(int confirmRow, int backRow)
{
    if (sFocusRow == confirmRow)
        return confirmActionForRow();
    if (sFocusRow == backRow)
        return CharCreateScreen::Action::Back;
    return CharCreateScreen::Action::None;
}

// Handle all keyboard input for CharCreateScreen. Returns the pending Action
// (None if no action was triggered this frame). Writes name edits to sName
// and focus-row changes to sFocusRow.
static CharCreateScreen::Action handleCharCreateKeyboard(EntityManager& em,
                                                         const AppearanceConfig* cfg, int visCount,
                                                         int confirmRow, int backRow, int firstRow)
{
    if (!sEditMode)
        handleNameTextInput(em);

    handleVerticalNavigation(em, firstRow, backRow);

    if (sFocusRow >= 0 && sFocusRow < visCount && cfg != nullptr)
        handleFocusedRowArrows(em, *cfg, sVisibleCats[sFocusRow]);

    CharCreateScreen::Action result = CharCreateScreen::Action::None;

    // Escape or RMB = back. Set result and fall through so the rest of the
    // frame draws normally -- an early return here would leave the frame
    // blank, causing a visible flash on the transition to LoadGame.
    if (keyPressed(em, SDL_SCANCODE_ESCAPE) || mouseClicked(em, SDL_BUTTON_RIGHT))
        result = CharCreateScreen::Action::Back;

    if (keyPressed(em, SDL_SCANCODE_RETURN) || keyPressed(em, SDL_SCANCODE_KP_ENTER))
    {
        const auto enterResult = handleEnterKey(confirmRow, backRow);
        if (enterResult != CharCreateScreen::Action::None)
            result = enterResult;
    }

    if (result != CharCreateScreen::Action::None)
    {
        SDL_StopTextInput();
        screen_input::playClickSfx(em);
    }
    return result;
}

// Draw the footer Start/Apply + Back/Cancel buttons. Updates sFocusRow on
// hover and returns an action if the user clicks one.
static CharCreateScreen::Action drawFooterButtons(EntityManager& em, float ww, float& cy, float mx,
                                                  float my, int confirmRow, int backRow)
{
    cy += 24.0f;
    const float btnPadX = 30.0f;
    const float btnPadY = 10.0f;
    const float btnGap = 16.0f;
    const float btnLineH = FontManager::lineHeight(sTitleFont);
    const float btnH = btnLineH + btnPadY * 2.0f;

    const char* confirmLabel = sEditMode ? "Apply" : "Start";
    const char* backLabel = sEditMode ? "Cancel" : "Back";
    const bool confirmDisabled = !sEditMode && !isNameValid();

    struct BtnDef
    {
        const char* label;
        int row;
        bool disabled;
    };
    const BtnDef buttons[2] = {{confirmLabel, confirmRow, confirmDisabled},
                               {backLabel, backRow, false}};

    CharCreateScreen::Action result = CharCreateScreen::Action::None;

    for (const auto& btn : buttons)
    {
        const TextSize sz = UIRenderer::measureText(sTitleFont, btn.label);
        const float bw = sz.width + btnPadX * 2.0f;
        const float bx = (ww - bw) * 0.5f;
        const bool hovered =
            !btn.disabled && mx >= bx && mx < bx + bw && my >= cy && my < cy + btnH;
        if (hovered)
            sFocusRow = btn.row;
        const bool selected = (sFocusRow == btn.row);
        const Color bgColor = (selected && !btn.disabled) ? BTN_BG_HL : BTN_BG;
        const Color fgColor = btn.disabled ? BTN_DIM : (selected ? BTN_HOVER : BTN_NORMAL);
        UIRenderer::drawRect(bx, cy, bw, btnH, bgColor);
        UIRenderer::drawText(sTitleFont, btn.label, bx + btnPadX, cy + btnPadY, fgColor);

        if (hovered && mouseClicked(em, SDL_BUTTON_LEFT) && !btn.disabled)
        {
            if (btn.row == confirmRow)
                result =
                    sEditMode ? CharCreateScreen::Action::Apply : CharCreateScreen::Action::Start;
            else
                result = CharCreateScreen::Action::Back;
            SDL_StopTextInput();
            screen_input::playClickSfx(em);
        }

        cy += btnH + btnGap;
    }

    return result;
}

static void lazyInitSelections(EntityManager& em, const AppearanceConfig& cfg)
{
    if (sInitialized)
        return;
    if (!sPendingAppearance.empty())
        initSelectionsFromMap(cfg, sPendingAppearance);
    else
        initSelectionsFromMap(cfg, {});
    sPendingAppearance.clear();
    recomposite(em);
    sCurrentSelections = buildSelections(cfg);
    sInitialized = true;
}

static void advanceWalkAnim()
{
    const int maxFrames = WALK_FRAMES;
    const uint32_t now = SDL_GetTicks();
    const float animDt = static_cast<float>(now - sLastTicks) * 0.001f;
    sLastTicks = now;
    sAnimTimer += animDt;
    if (sAnimTimer >= WALK_FRAME_DURATION)
    {
        sAnimTimer -= WALK_FRAME_DURATION;
        sAnimFrame = (sAnimFrame + 1) % maxFrames;
    }
}

static float getPreviewScale(const AppearanceConfig* cfg)
{
    if (cfg == nullptr || !cfg->loaded)
        return 1.0f;
    for (size_t i = 0; i < cfg->categories.size(); ++i)
    {
        const auto& cat = cfg->categories[i];
        if (cat.id == "size" && cat.type == AppearanceCategoryType::Slider &&
            i < sCatSliderValue.size())
            return sCatSliderValue[i];
    }
    return 1.0f;
}

static void drawPreview(float ww, float titleY, float titleH, const AppearanceConfig* cfg,
                        float& outBottomY)
{
    const float previewScale = getPreviewScale(cfg);
    const float basePreviewSize = 64.0f * PREVIEW_SCALE;
    const float maxPreviewSize = basePreviewSize * 1.20f; // matches layers.json max
    const float scaledPreviewSize = basePreviewSize * previewScale;
    const float previewBaseY = titleY + titleH + 20.0f;
    const float previewFootY = previewBaseY + maxPreviewSize;
    const float previewY = previewFootY - scaledPreviewSize;
    const float previewX = (ww - scaledPreviewSize) * 0.5f;

    if (sPreviewTex != 0 && sPreviewTexW > 0 && sPreviewTexH > 0)
    {
        const int col = sAnimFrame;
        const int sheetCols = sPreviewTexW / FRAME_PX;
        const int sheetRows = sPreviewTexH / FRAME_PX;
        const float u0 = static_cast<float>(col) / static_cast<float>(sheetCols);
        const float u1 = static_cast<float>(col + 1) / static_cast<float>(sheetCols);
        const float v0 = 1.0f / static_cast<float>(sheetRows);
        const float v1 = 2.0f / static_cast<float>(sheetRows);
        UIRenderer::drawTexturedRect(previewX, previewY, scaledPreviewSize, scaledPreviewSize,
                                     sPreviewTex, u0, v0, u1, v1);
    }
    else
    {
        UIRenderer::drawRect(previewX, previewY, scaledPreviewSize, scaledPreviewSize, FIELD_BG);
    }
    outBottomY = previewBaseY + maxPreviewSize;
}

static void drawNameField(float ww, float& cy, float lineH, float mx, float my)
{
    const float fieldW = 480.0f;
    const float fieldX = (ww - fieldW) * 0.5f;
    const float fieldH = lineH + 16.0f;
    UIRenderer::drawText(sBodyFont, "Name:", fieldX, cy, LABEL_COLOR);
    const float fieldY = cy + lineH + 6.0f;
    UIRenderer::drawRect(fieldX, fieldY, fieldW, fieldH, FIELD_BG);
    UIRenderer::drawRect(fieldX, fieldY, fieldW, 1.5f, FIELD_BORDER);
    UIRenderer::drawRect(fieldX, fieldY + fieldH - 1.5f, fieldW, 1.5f, FIELD_BORDER);
    UIRenderer::drawRect(fieldX, fieldY, 1.5f, fieldH, FIELD_BORDER);
    UIRenderer::drawRect(fieldX + fieldW - 1.5f, fieldY, 1.5f, fieldH, FIELD_BORDER);

    const float textY = fieldY + (fieldH - lineH) * 0.5f;
    const float adv = UIRenderer::drawText(sBodyFont, sName, fieldX + 12.0f, textY, TEXT_WHITE);

    if ((SDL_GetTicks() / 500) % 2 == 0)
    {
        const float curX = fieldX + 12.0f + adv + 2.0f;
        UIRenderer::drawRect(curX, textY, 2.0f, lineH, CURSOR_COLOR);
    }

    if (mx >= fieldX && mx < fieldX + fieldW && my >= fieldY && my < fieldY + fieldH)
        sFocusRow = -1;

    cy = fieldY + fieldH + 24.0f;
}

static float renderCategoryRow(EntityManager& em, const AppearanceConfig& cfg, int vi,
                               const RowLayout& lay)
{
    const int catIdx = sVisibleCats[vi];
    const auto& cat = cfg.categories[catIdx];
    const bool focused = (sFocusRow == vi);
    const int selIdx =
        (catIdx < static_cast<int>(sCatSelection.size())) ? sCatSelection[catIdx] : 0;

    if (cat.type == AppearanceCategoryType::Slider)
        return renderSliderRow(em, cfg, vi, catIdx, focused, lay);
    if (hasSwatch(cat))
        return renderSwatchRow(em, cfg, vi, catIdx, focused, selIdx, lay);
    return renderCycleRow(em, cfg, vi, catIdx, focused, selIdx, lay);
}

static void drawCategoryColumns(EntityManager& em, const AppearanceConfig& cfg, int visCount,
                                float cx, float& cy, float contentW, float rowH, float mx, float my)
{
    const float colW = (contentW - COL_GAP) * 0.5f;
    const float leftX = cx;
    const float rightX = cx + colW + COL_GAP;
    const int leftCount = (visCount + 1) / 2;

    const float colStartY = cy;
    float leftCy = colStartY;
    float rightCy = colStartY;

    for (int vi = 0; vi < visCount; ++vi)
    {
        const bool inLeftCol = (vi < leftCount);
        const float colX = inLeftCol ? leftX : rightX;
        float& colCy = inLeftCol ? leftCy : rightCy;
        const RowLayout lay{colX, colCy, colW, rowH, mx, my};
        colCy = renderCategoryRow(em, cfg, vi, lay);
    }

    cy = std::max(leftCy, rightCy);
}

CharCreateScreen::Action CharCreateScreen::render(EntityManager& em, int window_w, int window_h)
{
    ZoneScopedN("CharCreateScreen");

    const float ww = static_cast<float>(window_w);
    const float wh = static_cast<float>(window_h);
    const auto* cfg = em.registry().ctx().find<AppearanceConfig>();

    if (cfg != nullptr && cfg->loaded)
        lazyInitSelections(em, *cfg);

    const int visCount = static_cast<int>(sVisibleCats.size());
    const int confirmRow = visCount;
    const int backRow = visCount + 1;
    const int firstRow = sEditMode ? 0 : -1;

    advanceWalkAnim();

    Action result = handleCharCreateKeyboard(em, cfg, visCount, confirmRow, backRow, firstRow);

    // --- Drawing ---
    UIRenderer::drawRect(0.0f, 0.0f, ww, wh, OVERLAY_OPAQUE);

    const std::string title = sEditMode ? "Customize Character" : "Create Character";
    const TextSize tsz = UIRenderer::measureText(sTitleFont, title);
    const float titleY = wh * 0.06f;
    UIRenderer::drawText(sTitleFont, title, (ww - tsz.width) * 0.5f, titleY, TITLE_COLOR);

    float previewBottomY = 0.0f;
    drawPreview(ww, titleY, tsz.height, cfg, previewBottomY);

    float cy = previewBottomY + 24.0f;
    const float lineH = FontManager::lineHeight(sBodyFont);
    const float rowH = lineH + ROW_V_PAD;
    const float contentW = CONTENT_W;
    const float cx = (ww - contentW) * 0.5f;

    int mouseX = 0;
    int mouseY = 0;
    SDL_GetMouseState(&mouseX, &mouseY);
    const float mx = static_cast<float>(mouseX);
    const float my = static_cast<float>(mouseY);

    if (!sEditMode)
        drawNameField(ww, cy, lineH, mx, my);

    if (cfg != nullptr && cfg->loaded && visCount > 0)
        drawCategoryColumns(em, *cfg, visCount, cx, cy, contentW, rowH, mx, my);

    const Action btnResult = drawFooterButtons(em, ww, cy, mx, my, confirmRow, backRow);
    if (btnResult != Action::None)
        result = btnResult;

    return result;
}
